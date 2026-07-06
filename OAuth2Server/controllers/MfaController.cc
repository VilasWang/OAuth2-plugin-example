#include "MfaController.h"
#include <oauth2/utils/TotpUtils.h>
#include <oauth2/utils/CryptoUtils.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <oauth2/observability/AuditLogger.h>
#include <oauth2/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <drogon/drogon.h>
#include <chrono>

using namespace drogon;
using namespace drogon::orm;

namespace
{
// Emit an Application error via the unified ErrorResponder entry point so the
// body is always an Error Envelope (Requirement 7.1 / 7.3 / 7.5).
void respondError(
  const HttpRequestPtr &req,
  const std::shared_ptr<std::function<void(const HttpResponsePtr &)>> &cb,
  std::string code,
  std::string detailForLog = ""
)
{
    common::error::ErrorResponder::respond(
      req, [cb](const HttpResponsePtr &r) { (*cb)(r); }, std::move(code), std::move(detailForLog)
    );
}

struct MfaControllerDocs
{
    MfaControllerDocs()
    {
        oauth2::observability::openapi::EndpointInfo setupDocs;
        setupDocs.path = "/oauth2/mfa/setup";
        setupDocs.method = "POST";
        setupDocs.summary = "Setup MFA";
        setupDocs.description = "Initiate MFA setup by generating a TOTP secret.";
        setupDocs.tags = {"MFA"};
        setupDocs.requiresAuth = true;
        oauth2::observability::openapi::OpenApiGenerator::addEndpoint(setupDocs);

        oauth2::observability::openapi::EndpointInfo verifySetupDocs;
        verifySetupDocs.path = "/oauth2/mfa/setup/verify";
        verifySetupDocs.method = "POST";
        verifySetupDocs.summary = "Verify MFA Setup";
        verifySetupDocs.description = "Verify a TOTP code to finalize MFA setup.";
        verifySetupDocs.tags = {"MFA"};
        verifySetupDocs.requiresAuth = true;
        oauth2::observability::openapi::OpenApiGenerator::addEndpoint(verifySetupDocs);

        oauth2::observability::openapi::EndpointInfo disableDocs;
        disableDocs.path = "/oauth2/mfa/disable";
        disableDocs.method = "POST";
        disableDocs.summary = "Disable MFA";
        disableDocs.description = "Disable MFA for the authenticated user.";
        disableDocs.tags = {"MFA"};
        disableDocs.requiresAuth = true;
        oauth2::observability::openapi::OpenApiGenerator::addEndpoint(disableDocs);

        oauth2::observability::openapi::EndpointInfo verifyDocs;
        verifyDocs.path = "/oauth2/mfa/verify";
        verifyDocs.method = "POST";
        verifyDocs.summary = "Verify MFA Code (Login)";
        verifyDocs.description = "Verify MFA code during login.";
        verifyDocs.tags = {"MFA"};
        verifyDocs.requiresAuth = false;
        oauth2::observability::openapi::OpenApiGenerator::addEndpoint(verifyDocs);
    }
};

MfaControllerDocs docs_;
}  // namespace

void MfaController::setup(
  const HttpRequestPtr &req,
  std::function<void(const HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    auto sharedCb =
      std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));

    // Generate TOTP secret
    std::string secret = oauth2::utils::TotpUtils::generateSecret();

    // Store secret temporarily (not enabled until verified)
    auto db = app().getDbClient();
    db->execSqlAsync(
      "UPDATE users SET mfa_secret = $1 WHERE public_sub::text = $2::text",
      [sharedCb, secret, userId](const Result &) {
          std::string otpUri =
            oauth2::utils::TotpUtils::generateOtpAuthUri(secret, userId, "OAuth2Server");

          Json::Value json;
          json["secret"] = secret;
          json["otpauth_uri"] = otpUri;
          json["message"] = "Scan the QR code with your authenticator app, then verify with a code";
          auto resp = HttpResponse::newHttpJsonResponse(json);
          (*sharedCb)(resp);
      },
      [sharedCb, req](const DrogonDbException &e) {
          respondError(
            req, sharedCb, "DB_QUERY_ERROR", std::string("MFA setup failed: ") + e.base().what()
          );
      },
      secret,
      userId
    );
}

void MfaController::verifySetup(
  const HttpRequestPtr &req,
  std::function<void(const HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    std::string code;
    if (req->contentType() == CT_APPLICATION_JSON)
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
        common::error::ErrorResponder::respond(
          req,
          std::move(callback),
          "VALIDATION_FORMAT_ERROR",
          "verifySetup: 6-digit TOTP code is required"
        );
        return;
    }

    auto sharedCb =
      std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));

    auto db = app().getDbClient();
    db->execSqlAsync(
      "SELECT mfa_secret FROM users WHERE public_sub::text = $1::text",
      [sharedCb, code, userId, db, req](const Result &r) {
          if (r.empty() || r[0]["mfa_secret"].isNull())
          {
              respondError(
                req,
                sharedCb,
                "VALIDATION_INVALID_INPUT",
                "verifySetup: MFA not set up. Call /api/me/mfa/setup first"
              );
              return;
          }

          std::string secret = r[0]["mfa_secret"].as<std::string>();

          // Verify the TOTP code
          if (!oauth2::utils::TotpUtils::verifyCode(secret, code))
          {
              respondError(
                req, sharedCb, "AUTH_INVALID_CREDENTIALS", "verifySetup: TOTP code is incorrect"
              );
              return;
          }

          // Generate backup codes
          auto backupCodes = oauth2::utils::TotpUtils::generateBackupCodes(10);
          Json::Value codesJson(Json::arrayValue);
          Json::Value hashedCodesJson(Json::arrayValue);
          for (const auto &bc : backupCodes)
          {
              codesJson.append(bc);
              hashedCodesJson.append(oauth2::utils::hashToken(bc));
          }

          // Enable MFA
          Json::StreamWriterBuilder writer;
          writer["indentation"] = "";
          std::string hashedCodesStr = Json::writeString(writer, hashedCodesJson);

          db->execSqlAsync(
            "UPDATE users SET mfa_enabled = true, mfa_backup_codes = $1 "
            "WHERE public_sub::text = $2::text",
            [sharedCb, codesJson, userId, req](const Result &) {
                oauth2::observability::AuditLogger::log(
                  "mfa_enabled", "success", req, userId, "user", userId
                );
                Json::Value json;
                json["message"] = "MFA enabled successfully";
                json["backup_codes"] = codesJson;
                json["warning"] = "Save these backup codes securely. They cannot be shown again.";
                auto resp = HttpResponse::newHttpJsonResponse(json);
                (*sharedCb)(resp);
            },
            [sharedCb, req](const DrogonDbException &e) {
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
      [sharedCb, req](const DrogonDbException &e) {
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
  const HttpRequestPtr &req,
  std::function<void(const HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");

    auto sharedCb =
      std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));

    auto db = app().getDbClient();
    db->execSqlAsync(
      "UPDATE users SET mfa_enabled = false, mfa_secret = NULL, mfa_backup_codes = NULL "
      "WHERE public_sub::text = $1::text",
      [sharedCb](const Result &) {
          Json::Value json;
          json["message"] = "MFA disabled successfully";
          auto resp = HttpResponse::newHttpJsonResponse(json);
          (*sharedCb)(resp);
      },
      [sharedCb, req](const DrogonDbException &e) {
          respondError(
            req, sharedCb, "DB_QUERY_ERROR", std::string("MFA disable failed: ") + e.base().what()
          );
      },
      userId
    );
}

void MfaController::verifyLogin(
  const HttpRequestPtr &req,
  std::function<void(const HttpResponsePtr &)> &&callback
)
{
    // This endpoint is called during login when MFA is required
    // The mfa_token is a short-lived session token from the login step
    std::string mfaToken, code;
    // U-MFA-002 fix: after TOTP succeeds, we must issue real tokens (not just
    // a session flag) because the SPA is stateless and never sends cookies.
    // client_id/redirect_uri/scope/nonce are needed to generate the
    // authorization code the same way SessionController::login does; the SPA
    // already sends client_id and redirect_uri (see authService.ts
    // verifyMfa()). PKCE is intentionally out of scope here: the first-factor
    // /oauth2/login step does not use PKCE today, so there is no
    // code_verifier available to validate at this second step (see
    // PRD/mfa_auth_code_pkce_design.md §6/§7 for the follow-up architecture
    // proposal that would add PKCE across both steps).
    std::string clientId, redirectUri, scope, nonce;
    if (req->contentType() == CT_APPLICATION_JSON)
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
        common::error::ErrorResponder::respond(
          req,
          std::move(callback),
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "verifyLogin: mfa_token and code are required"
        );
        return;
    }

    if (clientId.empty() || redirectUri.empty())
    {
        common::error::ErrorResponder::respond(
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

    // For now, MFA login verification uses session-based approach
    // The mfa_token is the userId stored in session during first login step
    // In production, this should be a signed short-lived JWT
    auto sharedCb =
      std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));

    // Resolve the OAuth2 plugin once here (moved up from the TOTP-success
    // branch). It is needed for validateClient/validateRedirectUri (P0-1/P0-2
    // fix) AND for generateAuthorizationCode/exchangeCodeForToken below.
    auto plugin = drogon::app().getPlugin<::OAuth2Plugin>();
    if (!plugin)
    {
        respondError(req, sharedCb, "INTERNAL_ERROR", "verifyLogin: OAuth2 Plugin not loaded");
        return;
    }

    auto db = app().getDbClient();
    db->execSqlAsync(
      "SELECT id, public_sub, mfa_secret, mfa_backup_codes, mfa_pending_client_id, "
      "mfa_pending_redirect_uri FROM users WHERE id = $1",
      [sharedCb, code, mfaToken, req, clientId, redirectUri, scope, nonce, plugin](
        const Result &r
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
          // Pending first-factor login-session binding (written by
          // SessionController::login when it returned mfa_required). NULL means
          // no binding was recorded (e.g. a row that predates V022, or a
          // previous successful verification already cleared it).
          std::string pendingClientId =
            r[0]["mfa_pending_client_id"].isNull() ? "" : r[0]["mfa_pending_client_id"].as<std::string>();
          std::string pendingRedirectUri =
            r[0]["mfa_pending_redirect_uri"].isNull() ? "" : r[0]["mfa_pending_redirect_uri"].as<std::string>();

          // TOTP is checked first (unchanged ordering): a wrong code never
          // reveals anything about client/redirect_uri validity, preserving the
          // no-oracle property (Requirement 3.3, "regardless of client/
          // redirect_uri validity").
          if (oauth2::utils::TotpUtils::verifyCode(secret, code))
          {
              // MFA TOTP verified. Before issuing any code/token, enforce the
              // three P0-1/P0-2 checks. All rejections use
              // AUTH_INVALID_CREDENTIALS (401) so an unregistered client, a
              // non-whitelisted redirect_uri, and a mismatched pending binding
              // are indistinguishable from a wrong TOTP code from the outside
              // (no oracle for client registration).

              // Check 1 (P0-1, Requirement 2.1): the client must be registered.
              plugin->validateClient(
                clientId,
                "",  // empty secret: PUBLIC clients need no secret here
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

                    // Check 2 (P0-2, Requirement 2.2): redirect_uri must be in
                    // that client's registered whitelist (RFC 6749 §3.1.2.3).
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

                          // Check 3 (P0-1, Requirement 2.3): the
                          // client_id/redirect_uri must match the pending
                          // first-factor login-session binding recorded for
                          // this user. A NULL/empty pending binding is treated
                          // as a mismatch against any non-empty request pair.
                          if (clientId != pendingClientId ||
                              redirectUri != pendingRedirectUri)
                          {
                              respondError(
                                req,
                                sharedCb,
                                "AUTH_INVALID_CREDENTIALS",
                                "verifyLogin: client/redirect_uri does not match login session"
                              );
                              return;
                          }

                          // All checks passed: generate an authorization code
                          // and immediately exchange it for tokens server-side
                          // (no PKCE on this leg, see comment above), so the
                          // stateless SPA client gets access_token/refresh_token
                          // directly in this response instead of only a session
                          // flag (fixes U-MFA-002).
                          plugin->generateAuthorizationCode(
                            clientId,
                            publicSub,
                            scope,
                            redirectUri,
                            "",  // codeChallenge: PKCE not used on this leg
                            "",  // codeChallengeMethod
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
                                  "",  // clientSecret: vue-client is PUBLIC
                                  redirectUri,
                                  "",  // codeVerifier: no PKCE on this internal exchange
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

                                      oauth2::observability::AuditLogger::log(
                                        "mfa_verified", "success", req, publicSub, "user", publicSub
                                      );

                                      Json::Value json = tokenResult;
                                      json["message"] = "MFA verification successful";
                                      json["mfa_verified"] = true;

                                      // Best-effort: clear the pending binding now
                                      // that tokens are issued, so a later,
                                      // unrelated verification attempt has no
                                      // binding to reuse (Requirement 2.5). This
                                      // is best-effort (tokens are already issued
                                      // and cannot be un-issued): if the clear
                                      // fails we still return the tokens, only
                                      // LOG_ERROR for observability. mfaToken is
                                      // the users.id (the outer SELECT bound
                                      // WHERE id = $1 to mfaToken), so it is
                                      // reused directly.
                                      auto clearDb = app().getDbClient();
                                      auto sendSuccess = [sharedCb, req, json]() {
                                          auto resp = HttpResponse::newHttpJsonResponse(json);
                                          (*sharedCb)(resp);
                                      };
                                      if (clearDb)
                                      {
                                          clearDb->execSqlAsync(
                                            "UPDATE users SET mfa_pending_client_id = NULL, "
                                            "mfa_pending_redirect_uri = NULL WHERE id = $1",
                                            [sendSuccess](const Result &) { sendSuccess(); },
                                            [sendSuccess](const DrogonDbException &e) {
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

          // Try backup code
          // (simplified: in production, parse JSON array and check each hashed code)
          respondError(
            req, sharedCb, "AUTH_INVALID_CREDENTIALS", "verifyLogin: TOTP code is incorrect"
          );
      },
      [sharedCb, req](const DrogonDbException &e) {
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
