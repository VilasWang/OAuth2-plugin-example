#include <authforge/drogon/controllers/MfaController.h>
#include <authforge/drogon/utils/TotpUtils.h>
#include <authforge/drogon/utils/CryptoUtils.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <authforge/drogon/adapters/DrogonAuditSink.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <authforge/drogon/error/ErrorResponder.h>
#include <drogon/drogon.h>
#include <chrono>

// Task 24 slice 5 (authforge-sdk-refactor): identity-layer services this
// controller now optionally consumes.
#include <authforge/identity/IUserRepository.h>
#include <authforge/identity/MfaService.h>

#include <authforge/storage/postgres/models/Users.h>

using namespace ::drogon::orm;

namespace authforge::drogon::controllers
{

OAuth2Plugin *MfaController::resolvePlugin() const
{
    return plugin_ ? plugin_ : ::drogon::app().getPlugin<OAuth2Plugin>();
}

namespace
{

void respondError(
  const ::drogon::HttpRequestPtr &req,
  const std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>> &cb,
  std::string code,
  std::string detailForLog = ""
)
{
    ::authforge::common::error::ErrorResponder::respond(
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
        ::authforge::drogon::observability::openapi::EndpointInfo setupDocs;
        setupDocs.path = "/oauth2/mfa/setup";
        setupDocs.method = "POST";
        setupDocs.summary = "Setup MFA";
        setupDocs.description = "Initiate MFA setup by generating a TOTP secret.";
        setupDocs.tags = {"MFA"};
        setupDocs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(setupDocs);

        ::authforge::drogon::observability::openapi::EndpointInfo verifySetupDocs;
        verifySetupDocs.path = "/oauth2/mfa/setup/verify";
        verifySetupDocs.method = "POST";
        verifySetupDocs.summary = "Verify MFA Setup";
        verifySetupDocs.description = "Verify a TOTP code to finalize MFA setup.";
        verifySetupDocs.tags = {"MFA"};
        verifySetupDocs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(verifySetupDocs);

        ::authforge::drogon::observability::openapi::EndpointInfo disableDocs;
        disableDocs.path = "/oauth2/mfa/disable";
        disableDocs.method = "POST";
        disableDocs.summary = "Disable MFA";
        disableDocs.description = "Disable MFA for the authenticated user.";
        disableDocs.tags = {"MFA"};
        disableDocs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(disableDocs);

        ::authforge::drogon::observability::openapi::EndpointInfo verifyDocs;
        verifyDocs.path = "/oauth2/mfa/verify";
        verifyDocs.method = "POST";
        verifyDocs.summary = "Verify MFA Code (Login)";
        verifyDocs.description = "Verify MFA code during login.";
        verifyDocs.tags = {"MFA"};
        verifyDocs.requiresAuth = false;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(verifyDocs);
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

    // Task 24 slice 5: prefer the injected authforge::identity::MfaService
    // (constructed once at startup by
    // bootstrap::wireIdentityServices()/OAuth2Server/bootstrap/
    // IdentityAssembly.cc), falling back to the pre-Task-24 raw SQL below
    // when unwired -- same injected-with-fallback pattern established by
    // SessionController's Task 24 slice 4.
    if (mfaService_ && userRepo_)
    {
        userRepo_->findByPublicSub(
          userId, [this, sharedCb, req, userId](std::optional<authforge::identity::UserData> user) {
              if (!user)
              {
                  respondError(
                    req, sharedCb, "AUTH_INVALID_CREDENTIALS", "MFA setup: unknown user"
                  );
                  return;
              }
              mfaService_->setupSecret(
                user->id,
                userId,
                [sharedCb, req](std::optional<authforge::identity::MfaSetupResult> result) {
                    if (!result)
                    {
                        respondError(
                          req, sharedCb, "DB_QUERY_ERROR", "MFA setup: failed to store secret"
                        );
                        return;
                    }
                    Json::Value json;
                    json["secret"] = result->secret;
                    json["otpauth_uri"] = result->otpAuthUri;
                    json["message"] =
                      "Scan the QR code with your authenticator app, then verify with a code";
                    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                    (*sharedCb)(resp);
                }
              );
          }
        );
        return;
    }

    std::string secret = ::authforge::common::utils::TotpUtils::generateSecret();

    auto db = ::drogon::app().getDbClient();
    Mapper<drogon_model::oauth2_db::Users>(db).findBy(
      Criteria(drogon_model::oauth2_db::Users::Cols::_public_sub, CompareOperator::EQ, userId),
      [sharedCb, secret, userId, db, req](
        const std::vector<drogon_model::oauth2_db::Users> &users
      ) {
          if (users.empty())
          {
              respondError(
                req, sharedCb, "AUTH_INVALID_CREDENTIALS", "MFA setup failed: user not found"
              );
              return;
          }
          drogon_model::oauth2_db::Users updated = users[0];
          updated.setMfaSecret(secret);
          Mapper<drogon_model::oauth2_db::Users>(db).update(
            updated,
            [sharedCb, secret, userId](const size_t) {
                std::string otpUri = ::authforge::common::utils::TotpUtils::generateOtpAuthUri(
                  secret, userId, "OAuth2Server"
                );
                Json::Value json;
                json["secret"] = secret;
                json["otpauth_uri"] = otpUri;
                json["message"] =
                  "Scan the QR code with your authenticator app, then verify with a code";
                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                (*sharedCb)(resp);
            },
            [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req,
                  sharedCb,
                  "DB_QUERY_ERROR",
                  std::string("MFA setup failed: ") + e.base().what()
                );
            }
          );
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, sharedCb, "DB_QUERY_ERROR", std::string("MFA setup failed: ") + e.base().what()
          );
      }
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
        ::authforge::common::error::ErrorResponder::respond(
          req,
          std::move(callback),
          "VALIDATION_FORMAT_ERROR",
          "verifySetup: 6-digit TOTP code is required"
        );
        return;
    }

    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    // Task 24 slice 5: prefer the injected MfaService, same pattern as
    // setup() above.
    if (mfaService_ && userRepo_)
    {
        userRepo_->findByPublicSub(
          userId,
          [this, sharedCb, req, userId, code](std::optional<authforge::identity::UserData> user) {
              if (!user)
              {
                  respondError(
                    req, sharedCb, "AUTH_INVALID_CREDENTIALS", "verifySetup: unknown user"
                  );
                  return;
              }
              mfaService_->verifyAndEnable(
                user->id,
                code,
                [sharedCb,
                 req,
                 userId](std::optional<authforge::identity::MfaEnableResult> result) {
                    if (!result)
                    {
                        // MfaService::verifyAndEnable collapses "no
                        // secret set up" and "code mismatch" into one
                        // nullopt signal -- mirror the legacy path's
                        // more specific AUTH_MFA_NOT_CONFIGURED (the
                        // more actionable of the two for a client that
                        // never called /setup) since we cannot
                        // distinguish here.
                        respondError(
                          req,
                          sharedCb,
                          "AUTH_MFA_NOT_CONFIGURED",
                          "verifySetup: MFA not set up or TOTP code is incorrect"
                        );
                        return;
                    }
                    ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                      ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                      "mfa_enabled",
                      "success",
                      req,
                      userId,
                      "user",
                      userId
                    );
                    Json::Value codesJson(Json::arrayValue);
                    for (const auto &bc : result->backupCodes)
                        codesJson.append(bc);
                    Json::Value json;
                    json["message"] = "MFA enabled successfully";
                    json["backup_codes"] = codesJson;
                    json["warning"] =
                      "Save these backup codes securely. They cannot be shown again.";
                    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                    (*sharedCb)(resp);
                }
              );
          }
        );
        return;
    }

    auto db = ::drogon::app().getDbClient();
    Mapper<drogon_model::oauth2_db::Users>(db).findBy(
      Criteria(drogon_model::oauth2_db::Users::Cols::_public_sub, CompareOperator::EQ, userId),
      [sharedCb, code, userId, db, req](const std::vector<drogon_model::oauth2_db::Users> &users) {
          if (users.empty())
          {
              respondError(
                req,
                sharedCb,
                "AUTH_MFA_NOT_CONFIGURED",
                "verifySetup: MFA not set up. Call /api/me/mfa/setup first"
              );
              return;
          }

          std::string secret = users[0].getValueOfMfaSecret();
          if (secret.empty())
          {
              respondError(
                req,
                sharedCb,
                "AUTH_MFA_NOT_CONFIGURED",
                "verifySetup: MFA not set up. Call /api/me/mfa/setup first"
              );
              return;
          }

          if (!::authforge::common::utils::TotpUtils::verifyCode(secret, code))
          {
              respondError(
                req, sharedCb, "AUTH_MFA_CODE_INVALID", "verifySetup: TOTP code is incorrect"
              );
              return;
          }

          auto backupCodes = ::authforge::common::utils::TotpUtils::generateBackupCodes(10);
          Json::Value codesJson(Json::arrayValue);
          Json::Value hashedCodesJson(Json::arrayValue);
          for (const auto &bc : backupCodes)
          {
              codesJson.append(bc);
              hashedCodesJson.append(::authforge::drogon::utils::hashToken(bc));
          }

          Json::StreamWriterBuilder writer;
          writer["indentation"] = "";
          std::string hashedCodesStr = Json::writeString(writer, hashedCodesJson);

          drogon_model::oauth2_db::Users updated = users[0];
          updated.setMfaEnabled(true);
          updated.setMfaBackupCodes(hashedCodesStr);
          Mapper<drogon_model::oauth2_db::Users>(db).update(
            updated,
            [sharedCb, codesJson, userId, req](const size_t) {
                ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                  ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                  "mfa_enabled",
                  "success",
                  req,
                  userId,
                  "user",
                  userId
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
            }
          );
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req,
            sharedCb,
            "DB_QUERY_ERROR",
            std::string("MFA verify setup failed: ") + e.base().what()
          );
      }
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

    // Task 24 slice 5: prefer the injected MfaService, same pattern as
    // setup()/verifySetup() above.
    if (mfaService_ && userRepo_)
    {
        userRepo_->findByPublicSub(
          userId, [this, sharedCb, req](std::optional<authforge::identity::UserData> user) {
              if (!user)
              {
                  respondError(
                    req, sharedCb, "AUTH_INVALID_CREDENTIALS", "MFA disable: unknown user"
                  );
                  return;
              }
              mfaService_->disable(user->id, [sharedCb, req](bool ok) {
                  if (!ok)
                  {
                      respondError(
                        req, sharedCb, "DB_QUERY_ERROR", "MFA disable: repository write failed"
                      );
                      return;
                  }
                  Json::Value json;
                  json["message"] = "MFA disabled successfully";
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                  (*sharedCb)(resp);
              });
          }
        );
        return;
    }

    auto db = ::drogon::app().getDbClient();
    Mapper<drogon_model::oauth2_db::Users>(db).findBy(
      Criteria(drogon_model::oauth2_db::Users::Cols::_public_sub, CompareOperator::EQ, userId),
      [sharedCb, db, req](const std::vector<drogon_model::oauth2_db::Users> &users) {
          if (users.empty())
          {
              respondError(
                req, sharedCb, "AUTH_INVALID_CREDENTIALS", "MFA disable failed: user not found"
              );
              return;
          }
          drogon_model::oauth2_db::Users updated = users[0];
          updated.setMfaEnabled(false);
          updated.setMfaSecretToNull();
          updated.setMfaBackupCodesToNull();
          Mapper<drogon_model::oauth2_db::Users>(db).update(
            updated,
            [sharedCb](const size_t) {
                Json::Value json;
                json["message"] = "MFA disabled successfully";
                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                (*sharedCb)(resp);
            },
            [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req,
                  sharedCb,
                  "DB_QUERY_ERROR",
                  std::string("MFA disable failed: ") + e.base().what()
                );
            }
          );
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, sharedCb, "DB_QUERY_ERROR", std::string("MFA disable failed: ") + e.base().what()
          );
      }
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
        ::authforge::common::error::ErrorResponder::respond(
          req,
          std::move(callback),
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "verifyLogin: mfa_token and code are required"
        );
        return;
    }

    if (clientId.empty() || redirectUri.empty())
    {
        ::authforge::common::error::ErrorResponder::respond(
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

    auto plugin = resolvePlugin();
    if (!plugin)
    {
        respondError(req, sharedCb, "INTERNAL_ERROR", "verifyLogin: OAuth2 Plugin not loaded");
        return;
    }

    // Task 24 slice 5: continuation shared by both the injected-MfaService
    // path and the legacy raw-SQL fallback below -- both resolve to the
    // same (publicSub, pendingClientId, pendingRedirectUri, onSuccess)
    // shape once TOTP verification has already succeeded, so the
    // plugin->validateClient/validateRedirectUri/generateAuthorizationCode/
    // exchangeCodeForToken orchestration (an oauth2-domain concern, see
    // MfaService.h's own scope-boundary comment on why this stays outside
    // that class) is written once, not duplicated per path.
    auto onTotpVerified = [sharedCb, req, plugin, clientId, redirectUri, scope, nonce, mfaToken](
                            std::string publicSub,
                            std::string pendingClientId,
                            std::string pendingRedirectUri,
                            std::function<void(std::function<void()> &&)> clearPendingBinding
                          ) {
        plugin->validateClient(
          clientId,
          "",
          [sharedCb,
           req,
           plugin,
           clientId,
           redirectUri,
           publicSub,
           pendingClientId,
           pendingRedirectUri,
           scope,
           nonce,
           clearPendingBinding](bool validClient) {
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
                [sharedCb,
                 req,
                 plugin,
                 clientId,
                 redirectUri,
                 publicSub,
                 pendingClientId,
                 pendingRedirectUri,
                 scope,
                 nonce,
                 clearPendingBinding](bool validUri) {
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
                      [sharedCb,
                       req,
                       plugin,
                       clientId,
                       redirectUri,
                       publicSub,
                       clearPendingBinding](
                        bool success, std::string authCode, std::string genError
                      ) {
                          if (!success)
                          {
                              respondError(
                                req,
                                sharedCb,
                                "INTERNAL_ERROR",
                                "verifyLogin: failed to generate authorization code: " + genError
                              );
                              return;
                          }

                          plugin->exchangeCodeForToken(
                            authCode,
                            clientId,
                            "",
                            redirectUri,
                            "",
                            [sharedCb, req, publicSub, clearPendingBinding](
                              const Json::Value &tokenResult
                            ) {
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

                                ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                                  ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                                  "mfa_verified",
                                  "success",
                                  req,
                                  publicSub,
                                  "user",
                                  publicSub
                                );

                                Json::Value json = tokenResult;
                                json["message"] = "MFA verification successful";
                                json["mfa_verified"] = true;

                                auto sendSuccess = [sharedCb, json]() {
                                    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                                    (*sharedCb)(resp);
                                };
                                clearPendingBinding(std::move(sendSuccess));
                            }
                          );
                      }
                    );
                }
              );
          }
        );
    };

    // Task 24 slice 5: prefer the injected MfaService/IUserRepository,
    // falling back to the pre-Task-24 raw SQL when unwired -- same
    // injected-with-fallback pattern established by SessionController's
    // Task 24 slice 4. mfaToken IS the internal user id, stringified (see
    // SessionController::login()'s `mfaResp["mfa_token"] =
    // std::to_string(internalId);`), so no public_sub resolution step is
    // needed here.
    if (mfaService_ && userRepo_)
    {
        // int32: user ids are int32 end-to-end (Task 39 direction Y, DB int4);
        // std::stoi throws out_of_range for values beyond int32, which the
        // catch below already treats as an invalid MFA session.
        int32_t userId = 0;
        try
        {
            userId = std::stoi(mfaToken);
        }
        catch (const std::exception &)
        {
            respondError(
              req, sharedCb, "AUTH_INVALID_CREDENTIALS", "verifyLogin: invalid MFA session"
            );
            return;
        }

        userRepo_->findById(
          userId,
          [this, sharedCb, req, code, userId, onTotpVerified](
            std::optional<authforge::identity::UserData> user
          ) {
              if (!user)
              {
                  respondError(
                    req, sharedCb, "AUTH_INVALID_CREDENTIALS", "verifyLogin: invalid MFA session"
                  );
                  return;
              }
              std::string publicSub = user->publicSub;
              mfaService_->verifyLoginCode(
                userId,
                code,
                [this, sharedCb, req, userId, publicSub, onTotpVerified](bool codeValid) {
                    if (!codeValid)
                    {
                        respondError(
                          req,
                          sharedCb,
                          "AUTH_INVALID_CREDENTIALS",
                          "verifyLogin: TOTP code is incorrect"
                        );
                        return;
                    }
                    mfaService_->getPendingBinding(
                      userId,
                      [this, sharedCb, publicSub, userId, onTotpVerified](
                        std::optional<std::pair<std::string, std::string>> pending
                      ) {
                          std::string pendingClientId = pending ? pending->first : "";
                          std::string pendingRedirectUri = pending ? pending->second : "";
                          auto clearPendingBinding = [this, userId](std::function<void()> &&done) {
                              mfaService_->clearPendingBinding(
                                userId, [done = std::move(done)](bool) { done(); }
                              );
                          };
                          onTotpVerified(
                            publicSub, pendingClientId, pendingRedirectUri, clearPendingBinding
                          );
                      }
                    );
                }
              );
          }
        );
        return;
    }

    int32_t fallbackUserId = 0;
    try
    {
        fallbackUserId = std::stoi(mfaToken);
    }
    catch (const std::exception &)
    {
        respondError(req, sharedCb, "AUTH_INVALID_CREDENTIALS", "verifyLogin: invalid MFA session");
        return;
    }

    auto db = ::drogon::app().getDbClient();
    Mapper<drogon_model::oauth2_db::Users>(db).findBy(
      Criteria(drogon_model::oauth2_db::Users::Cols::_id, CompareOperator::EQ, fallbackUserId),
      [sharedCb, code, mfaToken, req, clientId, redirectUri, scope, nonce, plugin](
        const std::vector<drogon_model::oauth2_db::Users> &users
      ) {
          if (users.empty())
          {
              respondError(
                req, sharedCb, "AUTH_INVALID_CREDENTIALS", "verifyLogin: invalid MFA session"
              );
              return;
          }

          const auto &u = users[0];
          std::string secret = u.getValueOfMfaSecret();
          std::string publicSub = u.getValueOfPublicSub();
          auto pcid = u.getMfaPendingClientId();
          std::string pendingClientId = pcid ? *pcid : "";
          auto puri = u.getMfaPendingRedirectUri();
          std::string pendingRedirectUri = puri ? *puri : "";

          if (::authforge::common::utils::TotpUtils::verifyCode(secret, code))
          {
              plugin->validateClient(
                clientId,
                "",
                [sharedCb,
                 req,
                 plugin,
                 clientId,
                 redirectUri,
                 publicSub,
                 pendingClientId,
                 pendingRedirectUri,
                 scope,
                 nonce,
                 mfaToken](bool validClient) {
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
                      [sharedCb,
                       req,
                       plugin,
                       clientId,
                       redirectUri,
                       publicSub,
                       pendingClientId,
                       pendingRedirectUri,
                       scope,
                       nonce,
                       mfaToken](bool validUri) {
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
                                  [sharedCb, req, publicSub, mfaToken](
                                    const Json::Value &tokenResult
                                  ) {
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

                                      ::authforge::drogon::adapters::DrogonAuditSink::
                                        logFromRequest(
                                          ::drogon::app()
                                            .getPlugin<::OAuth2Plugin>()
                                            ->getAuditSink(),
                                          "mfa_verified",
                                          "success",
                                          req,
                                          publicSub,
                                          "user",
                                          publicSub
                                        );

                                      Json::Value json = tokenResult;
                                      json["message"] = "MFA verification successful";
                                      json["mfa_verified"] = true;

                                      auto clearDb = ::drogon::app().getDbClient();
                                      auto sendSuccess = [sharedCb, req, json]() {
                                          auto resp =
                                            ::drogon::HttpResponse::newHttpJsonResponse(json);
                                          (*sharedCb)(resp);
                                      };
                                      if (clearDb)
                                      {
                                          int32_t clearUserId = 0;
                                          try
                                          {
                                              clearUserId = std::stoi(mfaToken);
                                          }
                                          catch (const std::exception &)
                                          {
                                              sendSuccess();
                                              return;
                                          }
                                          Mapper<drogon_model::oauth2_db::Users>(clearDb).findBy(
                                            Criteria(
                                              drogon_model::oauth2_db::Users::Cols::_id,
                                              CompareOperator::EQ,
                                              clearUserId
                                            ),
                                            [sendSuccess, clearDb](
                                              const std::vector<drogon_model::oauth2_db::Users> &u
                                            ) {
                                                if (u.empty())
                                                {
                                                    sendSuccess();
                                                    return;
                                                }
                                                drogon_model::oauth2_db::Users clr = u[0];
                                                clr.setMfaPendingClientIdToNull();
                                                clr.setMfaPendingRedirectUriToNull();
                                                Mapper<drogon_model::oauth2_db::Users>(clearDb)
                                                  .update(
                                                    clr,
                                                    [sendSuccess](const size_t) { sendSuccess(); },
                                                    [sendSuccess](
                                                      const ::drogon::orm::DrogonDbException &e
                                                    ) {
                                                        // Recoverable: the
                                                        // request still succeeds.
                                                        LOG_WARN
                                                          << "verifyLogin: failed to clear MFA "
                                                             "pending binding: "
                                                          << e.base().what();
                                                        sendSuccess();
                                                    }
                                                  );
                                            },
                                            [sendSuccess](
                                              const ::drogon::orm::DrogonDbException &e
                                            ) {
                                                // Recoverable: the request
                                                // still succeeds.
                                                LOG_WARN << "verifyLogin: failed to find user for "
                                                            "MFA pending clear: "
                                                         << e.base().what();
                                                sendSuccess();
                                            }
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
      }
    );
}

}  // namespace authforge::drogon::controllers
