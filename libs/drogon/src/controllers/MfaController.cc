#include <authforge/drogon/controllers/MfaController.h>
#include <oauth2/utils/TotpUtils.h>
#include <oauth2/utils/CryptoUtils.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <oauth2/observability/AuditLogger.h>
#include <oauth2/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <drogon/drogon.h>
#include <chrono>

namespace authforge::drogon::controllers
{

namespace
{

void respondError(
  const ::drogon::HttpRequestPtr &req,
  const std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>> &cb,
  std::string code,
  std::string detailForLog = ""
)
{
    ::common::error::ErrorResponder::respond(
      req,
      [cb](const ::drogon::HttpResponsePtr &r) { (*cb)(r); },
      std::move(code),
      std::move(detailForLog)
    );
}

struct MfaControllerDocs
{
    MfaControllerDocs()
    {
        ::oauth2::observability::openapi::EndpointInfo setupDocs;
        setupDocs.path = "/oauth2/mfa/setup";
        setupDocs.method = "POST";
        setupDocs.summary = "Setup MFA";
        setupDocs.description = "Initiate MFA setup by generating a TOTP secret.";
        setupDocs.tags = {"MFA"};
        setupDocs.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(setupDocs);

        ::oauth2::observability::openapi::EndpointInfo verifySetupDocs;
        verifySetupDocs.path = "/oauth2/mfa/setup/verify";
        verifySetupDocs.method = "POST";
        verifySetupDocs.summary = "Verify MFA Setup";
        verifySetupDocs.description = "Verify a TOTP code to finalize MFA setup.";
        verifySetupDocs.tags = {"MFA"};
        verifySetupDocs.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(verifySetupDocs);

        ::oauth2::observability::openapi::EndpointInfo disableDocs;
        disableDocs.path = "/oauth2/mfa/disable";
        disableDocs.method = "POST";
        disableDocs.summary = "Disable MFA";
        disableDocs.description = "Disable MFA for the authenticated user.";
        disableDocs.tags = {"MFA"};
        disableDocs.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(disableDocs);

        ::oauth2::observability::openapi::EndpointInfo verifyDocs;
        verifyDocs.path = "/oauth2/mfa/verify";
        verifyDocs.method = "POST";
        verifyDocs.summary = "Verify MFA Code (Login)";
        verifyDocs.description = "Verify MFA code during login.";
        verifyDocs.tags = {"MFA"};
        verifyDocs.requiresAuth = false;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(verifyDocs);
    }
};

MfaControllerDocs docs_;

}  // namespace

void MfaController::setup(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    std::string secret = ::oauth2::utils::TotpUtils::generateSecret();

    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "UPDATE users SET mfa_secret = $1 WHERE public_sub::text = $2::text",
      [sharedCb, secret, userId](const ::drogon::orm::Result &) {
          std::string otpUri =
            ::oauth2::utils::TotpUtils::generateOtpAuthUri(secret, userId, "OAuth2Server");

          Json::Value json;
          json["secret"] = secret;
          json["otpauth_uri"] = otpUri;
          json["message"] = "Scan the QR code with your authenticator app, then verify with a code";
          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
          (*sharedCb)(resp);
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, sharedCb, "DB_QUERY_ERROR", std::string("MFA setup failed: ") + e.base().what()
          );
      },
      secret,
      userId
    );
}

void MfaController::verifySetup(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    std::string code;
    if (req->contentType() == ::drogon::CT_APPLICATION_JSON)
    {
        auto json = req->getJsonObject();
        if (json)
            code = json->get("code", "").asString();
    }
    else
    {
        code = req->getParameter("code");
    }

    if (code.empty() || code.length() != 6)
    {
        ::common::error::ErrorResponder::respond(
          req,
          std::move(callback),
          "VALIDATION_FORMAT_ERROR",
          "verifySetup: 6-digit TOTP code is required"
        );
        return;
    }

    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "SELECT mfa_secret FROM users WHERE public_sub::text = $1::text",
      [sharedCb, code, userId, db, req](const ::drogon::orm::Result &r) {
          if (r.empty() || r[0]["mfa_secret"].isNull())
          {
              respondError(
                req,
                sharedCb,
                "AUTH_MFA_NOT_CONFIGURED",
                "verifySetup: MFA not set up. Call /api/me/mfa/setup first"
              );
              return;
          }

          std::string secret = r[0]["mfa_secret"].as<std::string>();

          if (!::oauth2::utils::TotpUtils::verifyCode(secret, code))
          {
              respondError(
                req, sharedCb, "AUTH_MFA_CODE_INVALID", "verifySetup: TOTP code is incorrect"
              );
              return;
          }

          auto backupCodes = ::oauth2::utils::TotpUtils::generateBackupCodes(10);
          Json::Value codesJson(Json::arrayValue);
          Json::Value hashedCodesJson(Json::arrayValue);
          for (const auto &bc : backupCodes)
          {
              codesJson.append(bc);
              hashedCodesJson.append(::oauth2::utils::hashToken(bc));
          }

          Json::StreamWriterBuilder writer;
          writer["indentation"] = "";
          std::string hashedCodesStr = Json::writeString(writer, hashedCodesJson);

          db->execSqlAsync(
            "UPDATE users SET mfa_enabled = true, mfa_backup_codes = $1 "
            "WHERE public_sub::text = $2::text",
            [sharedCb, codesJson, userId, req](const ::drogon::orm::Result &) {
                ::oauth2::observability::AuditLogger::log(
                  "mfa_enabled", "success", req, userId, "user", userId
                );
                Json::Value json;
                json["message"] = "MFA enabled successfully";
                json["backup_codes"] = codesJson;
                json["warning"] = "Save these backup codes securely. They cannot be shown again.";
                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                (*sharedCb)(resp);
            },
            [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req,
                  sharedCb,
                  "DB_QUERY_ERROR",
                  std::string("MFA enable failed: ") + e.base().what()
                );
            },
            hashedCodesStr,
            userId
          );
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req,
            sharedCb,
            "DB_QUERY_ERROR",
            std::string("MFA verify setup failed: ") + e.base().what()
          );
      },
      userId
    );
}

void MfaController::disable(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");

    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "UPDATE users SET mfa_enabled = false, mfa_secret = NULL, mfa_backup_codes = NULL "
      "WHERE public_sub::text = $1::text",
      [sharedCb](const ::drogon::orm::Result &) {
          Json::Value json;
          json["message"] = "MFA disabled successfully";
          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
          (*sharedCb)(resp);
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, sharedCb, "DB_QUERY_ERROR", std::string("MFA disable failed: ") + e.base().what()
          );
      },
      userId
    );
}

void MfaController::verifyLogin(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    std::string mfaToken, code;
    std::string clientId, redirectUri, scope, nonce;
    if (req->contentType() == ::drogon::CT_APPLICATION_JSON)
    {
        auto json = req->getJsonObject();
        if (json)
        {
            mfaToken = json->get("mfa_token", "").asString();
            code = json->get("code", "").asString();
            clientId = json->get("client_id", "").asString();
            redirectUri = json->get("redirect_uri", "").asString();
            scope = json->get("scope", "").asString();
            nonce = json->get("nonce", "").asString();
        }
    }
    else
    {
        mfaToken = req->getParameter("mfa_token");
        code = req->getParameter("code");
        clientId = req->getParameter("client_id");
        redirectUri = req->getParameter("redirect_uri");
        scope = req->getParameter("scope");
        nonce = req->getParameter("nonce");
    }

    if (mfaToken.empty() || code.empty())
    {
        ::common::error::ErrorResponder::respond(
          req,
          std::move(callback),
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "verifyLogin: mfa_token and code are required"
        );
        return;
    }

    if (clientId.empty() || redirectUri.empty())
    {
        ::common::error::ErrorResponder::respond(
          req,
          std::move(callback),
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "verifyLogin: client_id and redirect_uri are required to issue tokens after MFA"
        );
        return;
    }
    if (scope.empty())
    {
        scope = "openid profile email";
    }

    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto plugin = ::drogon::app().getPlugin<::OAuth2Plugin>();
    if (!plugin)
    {
        respondError(req, sharedCb, "INTERNAL_ERROR", "verifyLogin: OAuth2 Plugin not loaded");
        return;
    }

    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "SELECT id, public_sub, mfa_secret, mfa_backup_codes, mfa_pending_client_id, "
      "mfa_pending_redirect_uri FROM users WHERE id = $1",
      [sharedCb, code, mfaToken, req, clientId, redirectUri, scope, nonce, plugin](
        const ::drogon::orm::Result &r
      ) {
          if (r.empty())
          {
              respondError(
                req, sharedCb, "AUTH_INVALID_CREDENTIALS", "verifyLogin: invalid MFA session"
              );
              return;
          }

          std::string secret =
            r[0]["mfa_secret"].isNull() ? "" : r[0]["mfa_secret"].as<std::string>();
          std::string publicSub = r[0]["public_sub"].as<std::string>();
          std::string pendingClientId = r[0]["mfa_pending_client_id"].isNull()
                                           ? ""
                                           : r[0]["mfa_pending_client_id"].as<std::string>();
          std::string pendingRedirectUri = r[0]["mfa_pending_redirect_uri"].isNull()
                                              ? ""
                                              : r[0]["mfa_pending_redirect_uri"].as<std::string>();

          if (::oauth2::utils::TotpUtils::verifyCode(secret, code))
          {
              plugin->validateClient(
                clientId,
                "",
                [sharedCb, req, plugin, clientId, redirectUri, publicSub, pendingClientId,
                 pendingRedirectUri, scope, nonce, mfaToken](bool validClient) {
                    if (!validClient)
                    {
                        respondError(
                          req,
                          sharedCb,
                          "AUTH_INVALID_CREDENTIALS",
                          "verifyLogin: unknown or invalid client"
                        );
                        return;
                    }

                    plugin->validateRedirectUri(
                      clientId,
                      redirectUri,
                      [sharedCb, req, plugin, clientId, redirectUri, publicSub, pendingClientId,
                       pendingRedirectUri, scope, nonce, mfaToken](bool validUri) {
                          if (!validUri)
                          {
                              respondError(
                                req,
                                sharedCb,
                                "AUTH_INVALID_CREDENTIALS",
                                "verifyLogin: redirect_uri not registered for client"
                              );
                              return;
                          }

                          if (clientId != pendingClientId || redirectUri != pendingRedirectUri)
                          {
                              respondError(
                                req,
                                sharedCb,
                                "AUTH_INVALID_CREDENTIALS",
                                "verifyLogin: client/redirect_uri does not match login session"
                              );
                              return;
                          }

                          plugin->generateAuthorizationCode(
                            clientId,
                            publicSub,
                            scope,
                            redirectUri,
                            "",
                            "",
                            nonce,
                            [sharedCb, req, plugin, clientId, redirectUri, publicSub, mfaToken](
                              bool success, std::string authCode, std::string genError
                            ) {
                                if (!success)
                                {
                                    respondError(
                                      req,
                                      sharedCb,
                                      "INTERNAL_ERROR",
                                      "verifyLogin: failed to generate authorization code: " +
                                        genError
                                    );
                                    return;
                                }

                                plugin->exchangeCodeForToken(
                                  authCode,
                                  clientId,
                                  "",
                                  redirectUri,
                                  "",
                                  [sharedCb, req, publicSub, mfaToken](const Json::Value &tokenResult) {
                                      if (tokenResult.isMember("error"))
                                      {
                                          std::string detail =
                                            tokenResult.isMember("error_description")
                                              ? tokenResult["error_description"].asString()
                                              : tokenResult["error"].asString();
                                          respondError(
                                            req,
                                            sharedCb,
                                            "INTERNAL_ERROR",
                                            "verifyLogin: failed to exchange authorization code: " +
                                              detail
                                          );
                                          return;
                                      }

                                      ::oauth2::observability::AuditLogger::log(
                                        "mfa_verified", "success", req, publicSub, "user", publicSub
                                      );

                                      Json::Value json = tokenResult;
                                      json["message"] = "MFA verification successful";
                                      json["mfa_verified"] = true;

                                      auto clearDb = ::drogon::app().getDbClient();
                                      auto sendSuccess = [sharedCb, req, json]() {
                                          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                                          (*sharedCb)(resp);
                                      };
                                      if (clearDb)
                                      {
                                          clearDb->execSqlAsync(
                                            "UPDATE users SET mfa_pending_client_id = NULL, "
                                            "mfa_pending_redirect_uri = NULL WHERE id = $1",
                                            [sendSuccess](const ::drogon::orm::Result &) { sendSuccess(); },
                                            [sendSuccess](const ::drogon::orm::DrogonDbException &e) {
                                                LOG_ERROR
                                                  << "verifyLogin: failed to clear MFA pending "
                                                     "binding (tokens already issued): "
                                                  << e.base().what();
                                                sendSuccess();
                                            },
                                            mfaToken
                                          );
                                      }
                                      else
                                      {
                                          sendSuccess();
                                      }
                                  }
                                );
                            }
                          );
                      }
                    );
                }
              );
              return;
          }

          respondError(
            req, sharedCb, "AUTH_INVALID_CREDENTIALS", "verifyLogin: TOTP code is incorrect"
          );
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req,
            sharedCb,
            "DB_QUERY_ERROR",
            std::string("MFA login verify failed: ") + e.base().what()
          );
      },
      mfaToken
    );
}

}  // namespace authforge::drogon::controllers
