#include <authforge/drogon/controllers/WebAuthnController.h>
#include <authforge/drogon/utils/CryptoUtils.h>
#include <authforge/drogon/adapters/DrogonAuditSink.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <authforge/drogon/error/ErrorResponder.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <chrono>

// Task 24 slice 5 (authforge-sdk-refactor): identity-layer services this
// controller now optionally consumes.
#include <authforge/identity/IUserRepository.h>
#include <authforge/identity/WebAuthnService.h>

#include <authforge/storage/postgres/models/Users.h>
#include <authforge/storage/postgres/models/WebauthnCredentials.h>

using namespace ::drogon::orm;

namespace authforge::drogon::controllers
{

namespace
{
// Emit an Application error via the unified ErrorResponder entry point so the
// body is always an Error Envelope (Requirement 7.1 / 7.3 / 7.5).
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

struct WebAuthnControllerDocs
{
    WebAuthnControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo regBeginDocs;
        regBeginDocs.path = "/api/me/webauthn/register/begin";
        regBeginDocs.method = "POST";
        regBeginDocs.summary = "WebAuthn Register Begin";
        regBeginDocs.description = "Start WebAuthn registration.";
        regBeginDocs.tags = {"WebAuthn"};
        regBeginDocs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(regBeginDocs);

        ::authforge::drogon::observability::openapi::EndpointInfo regFinishDocs;
        regFinishDocs.path = "/api/me/webauthn/register/finish";
        regFinishDocs.method = "POST";
        regFinishDocs.summary = "WebAuthn Register Finish";
        regFinishDocs.description = "Finish WebAuthn registration.";
        regFinishDocs.tags = {"WebAuthn"};
        regFinishDocs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(regFinishDocs);

        ::authforge::drogon::observability::openapi::EndpointInfo loginBeginDocs;
        loginBeginDocs.path = "/oauth2/webauthn/authenticate/begin";
        loginBeginDocs.method = "POST";
        loginBeginDocs.summary = "WebAuthn Authenticate Begin";
        loginBeginDocs.description = "Start WebAuthn authentication.";
        loginBeginDocs.tags = {"WebAuthn"};
        loginBeginDocs.requiresAuth = false;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(loginBeginDocs);

        ::authforge::drogon::observability::openapi::EndpointInfo loginFinishDocs;
        loginFinishDocs.path = "/oauth2/webauthn/authenticate/finish";
        loginFinishDocs.method = "POST";
        loginFinishDocs.summary = "WebAuthn Authenticate Finish";
        loginFinishDocs.description = "Finish WebAuthn authentication.";
        loginFinishDocs.tags = {"WebAuthn"};
        loginFinishDocs.requiresAuth = false;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(loginFinishDocs);

        ::authforge::drogon::observability::openapi::EndpointInfo credentialsDocs;
        credentialsDocs.path = "/api/me/webauthn/credentials";
        credentialsDocs.method = "GET";
        credentialsDocs.summary = "List WebAuthn Credentials";
        credentialsDocs.description = "List registered WebAuthn credentials.";
        credentialsDocs.tags = {"WebAuthn"};
        credentialsDocs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(credentialsDocs);
    }
};

WebAuthnControllerDocs docs_;

// WebAuthn RP (Relying Party) configuration
std::string getRpId()
{
    auto config = ::drogon::app().getCustomConfig();
    if (config.isMember("webauthn") && config["webauthn"].isMember("rp_id"))
        return config["webauthn"]["rp_id"].asString();
    return "localhost";
}

std::string getRpName()
{
    auto config = ::drogon::app().getCustomConfig();
    if (config.isMember("webauthn") && config["webauthn"].isMember("rp_name"))
        return config["webauthn"]["rp_name"].asString();
    return "OAuth2 Server";
}
}  // namespace

void WebAuthnController::registerBegin(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");

    // Task 24 slice 5: prefer the injected WebAuthnService (challenge
    // generation only -- see WebAuthnService.h's own top comment on why
    // it hands the challenge back rather than storing it itself), falling
    // back to the pre-Task-24 direct generateSecureToken() call when
    // unwired.
    if (webAuthnService_)
    {
        auto sharedCb = std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
          std::move(callback)
        );
        webAuthnService_->beginRegistration(
          [sharedCb,
           req,
           userId](std::optional<authforge::identity::WebAuthnRegistrationChallenge> result) {
              if (!result)
              {
                  respondError(
                    req, sharedCb, "INTERNAL_ERROR", "registerBegin: challenge generation failed"
                  );
                  return;
              }
              if (req->session())
                  req->session()->insert("webauthn_challenge", result->challenge);

              Json::Value options;
              options["challenge"] = result->challenge;
              Json::Value rp;
              rp["id"] = result->rpId;
              rp["name"] = result->rpName;
              options["rp"] = rp;
              Json::Value user;
              user["id"] = userId;
              user["name"] = userId;
              user["displayName"] = userId;
              options["user"] = user;
              Json::Value pubKeyCredParams(Json::arrayValue);
              Json::Value es256;
              es256["type"] = "public-key";
              es256["alg"] = -7;
              pubKeyCredParams.append(es256);
              Json::Value rs256;
              rs256["type"] = "public-key";
              rs256["alg"] = -257;
              pubKeyCredParams.append(rs256);
              options["pubKeyCredParams"] = pubKeyCredParams;
              options["timeout"] = result->timeoutMs;
              Json::Value authenticatorSelection;
              authenticatorSelection["userVerification"] = "preferred";
              authenticatorSelection["residentKey"] = "preferred";
              options["authenticatorSelection"] = authenticatorSelection;
              Json::Value response;
              response["options"] = options;
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(response));
          }
        );
        return;
    }

    // Generate challenge (32 bytes, base64url encoded)
    std::string challenge = ::authforge::drogon::utils::generateSecureToken();

    // Store challenge in session for verification in registerFinish
    if (req->session())
    {
        req->session()->insert("webauthn_challenge", challenge);
    }

    // Build PublicKeyCredentialCreationOptions
    Json::Value options;
    options["challenge"] = challenge;

    Json::Value rp;
    rp["id"] = getRpId();
    rp["name"] = getRpName();
    options["rp"] = rp;

    Json::Value user;
    user["id"] = userId;  // base64url of user ID
    user["name"] = userId;
    user["displayName"] = userId;
    options["user"] = user;

    // Supported algorithms (ES256 preferred, RS256 fallback)
    Json::Value pubKeyCredParams(Json::arrayValue);
    Json::Value es256;
    es256["type"] = "public-key";
    es256["alg"] = -7;  // ES256
    pubKeyCredParams.append(es256);
    Json::Value rs256;
    rs256["type"] = "public-key";
    rs256["alg"] = -257;  // RS256
    pubKeyCredParams.append(rs256);
    options["pubKeyCredParams"] = pubKeyCredParams;

    options["timeout"] = 60000;  // 60 seconds

    Json::Value authenticatorSelection;
    authenticatorSelection["userVerification"] = "preferred";
    authenticatorSelection["residentKey"] = "preferred";
    options["authenticatorSelection"] = authenticatorSelection;

    Json::Value response;
    response["options"] = options;
    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

void WebAuthnController::registerFinish(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_INVALID_INPUT",
          "registerFinish: JSON body with credential response required"
        );
        return;
    }

    std::string credentialId = (*jsonBody).get("credential_id", "").asString();
    std::string publicKey = (*jsonBody).get("public_key", "").asString();
    std::string credName = (*jsonBody).get("name", "Passkey").asString();

    if (credentialId.empty() || publicKey.empty())
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "registerFinish: credential_id and public_key are required"
        );
        return;
    }

    // Task 24 slice 5: prefer the injected WebAuthnService/IUserRepository,
    // falling back to the pre-Task-24 raw SQL when unwired.
    if (webAuthnService_ && userRepo_)
    {
        userRepo_->findByPublicSub(
          userId,
          [this, sharedCb, req, userId, credentialId, publicKey, credName](
            std::optional<authforge::identity::UserData> user
          ) {
              if (!user)
              {
                  respondError(
                    req, sharedCb, "AUTH_INVALID_CREDENTIALS", "registerFinish: unknown user"
                  );
                  return;
              }
              webAuthnService_->finishRegistration(
                user->id,
                credentialId,
                publicKey,
                credName,
                [sharedCb, req, userId, credentialId](const std::string &errorCode) {
                    if (!errorCode.empty())
                    {
                        respondError(req, sharedCb, errorCode, "registerFinish: " + errorCode);
                        return;
                    }
                    ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                      ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                      "webauthn_registered",
                      "success",
                      req,
                      userId,
                      "credential",
                      credentialId
                    );
                    Json::Value json;
                    json["message"] = "Passkey registered successfully";
                    json["credential_id"] = credentialId;
                    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                    resp->setStatusCode(::drogon::k201Created);
                    (*sharedCb)(resp);
                }
              );
          }
        );
        return;
    }

    // Store credential: first resolve public_sub to user_id, then insert
    auto db = ::drogon::app().getDbClient();
    Mapper<drogon_model::oauth2_db::Users>(db).findBy(
      Criteria(drogon_model::oauth2_db::Users::Cols::_public_sub, CompareOperator::EQ, userId),
      [db, sharedCb, req, credentialId, publicKey, credName](
        const std::vector<drogon_model::oauth2_db::Users> &usersResult
      ) {
          if (usersResult.empty())
          {
              respondError(
                req, sharedCb, "AUTH_INVALID_CREDENTIALS", "registerFinish: user not found"
              );
              return;
          }
          int32_t resolvedUserId = usersResult[0].getValueOfId();

          drogon_model::oauth2_db::WebauthnCredentials cred;
          cred.setUserId(resolvedUserId);
          cred.setCredentialId(credentialId);
          cred.setPublicKey(publicKey);
          cred.setName(credName);

          Mapper<drogon_model::oauth2_db::WebauthnCredentials>(db).insert(
            cred,
            [sharedCb, credentialId, req](const drogon_model::oauth2_db::WebauthnCredentials &) {
                ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                  ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                  "webauthn_registered",
                  "success",
                  req,
                  "",
                  "credential",
                  credentialId
                );
                Json::Value json;
                json["message"] = "Passkey registered successfully";
                json["credential_id"] = credentialId;
                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                resp->setStatusCode(::drogon::k201Created);
                (*sharedCb)(resp);
            },
            [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                const std::string what = e.base().what();
                if (
                  what.find("webauthn_credentials") != std::string::npos &&
                  what.find("credential_id") != std::string::npos
                )
                {
                    respondError(
                      req,
                      sharedCb,
                      "VALIDATION_CREDENTIAL_ALREADY_REGISTERED",
                      std::string("registerFinish: duplicate credential_id: ") + what
                    );
                    return;
                }
                respondError(
                  req,
                  sharedCb,
                  "DB_QUERY_ERROR",
                  std::string("registerFinish: failed to store credential: ") + what
                );
            }
          );
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req,
            sharedCb,
            "DB_QUERY_ERROR",
            std::string("registerFinish: user lookup failed: ") + e.base().what()
          );
      }
    );
}

void WebAuthnController::authenticateBegin(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    // Task 24 slice 5: prefer the injected WebAuthnService, falling back
    // to the pre-Task-24 direct generateSecureToken() call when unwired.
    if (webAuthnService_)
    {
        auto sharedCb = std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
          std::move(callback)
        );
        webAuthnService_->beginAuthentication(
          [sharedCb,
           req](std::optional<authforge::identity::WebAuthnAuthenticationChallenge> result) {
              if (!result)
              {
                  respondError(
                    req,
                    sharedCb,
                    "INTERNAL_ERROR",
                    "authenticateBegin: challenge generation failed"
                  );
                  return;
              }
              if (req->session())
                  req->session()->insert("webauthn_auth_challenge", result->challenge);

              Json::Value options;
              options["challenge"] = result->challenge;
              options["rpId"] = result->rpId;
              options["timeout"] = result->timeoutMs;
              options["userVerification"] = "preferred";
              options["allowCredentials"] = Json::Value(Json::arrayValue);
              Json::Value response;
              response["options"] = options;
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(response));
          }
        );
        return;
    }

    // Generate challenge
    std::string challenge = ::authforge::drogon::utils::generateSecureToken();

    // Store in session
    if (req->session())
    {
        req->session()->insert("webauthn_auth_challenge", challenge);
    }

    Json::Value options;
    options["challenge"] = challenge;
    options["rpId"] = getRpId();
    options["timeout"] = 60000;
    options["userVerification"] = "preferred";

    // Allow any credential (discoverable/resident key flow)
    options["allowCredentials"] = Json::Value(Json::arrayValue);

    Json::Value response;
    response["options"] = options;
    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

void WebAuthnController::authenticateFinish(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(
          req, sharedCb, "VALIDATION_INVALID_INPUT", "authenticateFinish: JSON body is required"
        );
        return;
    }

    std::string credentialId = (*jsonBody).get("credential_id", "").asString();
    if (credentialId.empty())
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "authenticateFinish: credential_id is required"
        );
        return;
    }

    // Task 24 slice 5: prefer the injected WebAuthnService, falling back
    // to the pre-Task-24 raw SQL when unwired.
    if (webAuthnService_)
    {
        webAuthnService_->finishAuthentication(
          credentialId,
          [sharedCb,
           req,
           credentialId](std::optional<authforge::identity::WebAuthnAuthResult> result) {
              if (!result)
              {
                  respondError(
                    req,
                    sharedCb,
                    "AUTH_INVALID_CREDENTIALS",
                    "authenticateFinish: credential not found"
                  );
                  return;
              }
              ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                "webauthn_authenticated",
                "success",
                req,
                result->publicSub,
                "credential",
                credentialId
              );
              Json::Value json;
              json["authenticated"] = true;
              json["user_id"] = result->publicSub;
              json["sign_count"] = result->signCount;
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          }
        );
        return;
    }

    // Look up credential and verify (split JOIN into two Mapper queries)
    auto db = ::drogon::app().getDbClient();
    Mapper<drogon_model::oauth2_db::WebauthnCredentials>(db).findBy(
      Criteria(
        drogon_model::oauth2_db::WebauthnCredentials::Cols::_credential_id,
        CompareOperator::EQ,
        credentialId
      ),
      [sharedCb, credentialId, db, req](
        const std::vector<drogon_model::oauth2_db::WebauthnCredentials> &creds
      ) {
          if (creds.empty())
          {
              respondError(
                req,
                sharedCb,
                "AUTH_INVALID_CREDENTIALS",
                "authenticateFinish: credential not found"
              );
              return;
          }

          const auto &wc = creds[0];
          int userId = wc.getValueOfUserId();
          int signCount = wc.getValueOfSignCount();

          // Build credential update from already-fetched object
          int newSignCount = signCount + 1;
          auto credUpdate = std::make_shared<drogon_model::oauth2_db::WebauthnCredentials>(wc);
          credUpdate->setSignCount(newSignCount);
          credUpdate->setLastUsedAt(::trantor::Date::now());

          // Query user for public_sub
          Mapper<drogon_model::oauth2_db::Users>(db).findBy(
            Criteria(drogon_model::oauth2_db::Users::Cols::_id, CompareOperator::EQ, userId),
            [sharedCb, credentialId, db, req, userId, newSignCount, credUpdate](
              const std::vector<drogon_model::oauth2_db::Users> &users
            ) {
                std::string publicSub = users.empty() ? "" : users[0].getValueOfPublicSub();

                // Update sign_count and last_used_at (reuse outer findBy result)
                Mapper<drogon_model::oauth2_db::WebauthnCredentials>(db).update(
                  *credUpdate,
                  [](const size_t) {},
                  [](const ::drogon::orm::DrogonDbException &e) {
                      LOG_WARN << "Failed to update sign count: " << e.base().what();
                  }
                );

                ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                  ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                  "webauthn_authenticated",
                  "success",
                  req,
                  publicSub,
                  "credential",
                  credentialId
                );

                Json::Value json;
                json["authenticated"] = true;
                json["user_id"] = publicSub;
                json["sign_count"] = newSignCount;
                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                (*sharedCb)(resp);
            },
            [sharedCb, req](const ::drogon::orm::DrogonDbException &) {
                respondError(
                  req, sharedCb, "DB_QUERY_ERROR", "authenticateFinish: DB error fetching user"
                );
            }
          );
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req,
            sharedCb,
            "DB_QUERY_ERROR",
            std::string("authenticateFinish: lookup failed: ") + e.base().what()
          );
      }
    );
}

void WebAuthnController::listCredentials(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    // Task 24 slice 5: prefer the injected WebAuthnService/IUserRepository,
    // falling back to the pre-Task-24 raw SQL when unwired.
    if (webAuthnService_ && userRepo_)
    {
        userRepo_->findByPublicSub(
          userId, [this, sharedCb](std::optional<authforge::identity::UserData> user) {
              if (!user)
              {
                  Json::Value json;
                  json["credentials"] = Json::Value(Json::arrayValue);
                  json["total"] = 0;
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                  return;
              }
              webAuthnService_->listCredentials(
                user->id,
                [sharedCb](std::vector<authforge::identity::WebAuthnCredentialSummary> creds) {
                    Json::Value json;
                    Json::Value credsJson(Json::arrayValue);
                    for (const auto &c : creds)
                    {
                        Json::Value cred;
                        cred["credential_id"] = c.credentialId;
                        cred["name"] = c.name;
                        cred["sign_count"] = c.signCount;
                        credsJson.append(cred);
                    }
                    json["credentials"] = credsJson;
                    json["total"] = static_cast<int>(creds.size());
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                }
              );
          }
        );
        return;
    }

    auto db = ::drogon::app().getDbClient();
    // First resolve public_sub to user_id, then list credentials
    Mapper<drogon_model::oauth2_db::Users>(db).findBy(
      Criteria(drogon_model::oauth2_db::Users::Cols::_public_sub, CompareOperator::EQ, userId),
      [sharedCb, db, req](const std::vector<drogon_model::oauth2_db::Users> &users) {
          if (users.empty())
          {
              Json::Value json;
              json["credentials"] = Json::Value(Json::arrayValue);
              json["total"] = 0;
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              return;
          }
          int32_t resolvedId = users[0].getValueOfId();

          Mapper<drogon_model::oauth2_db::WebauthnCredentials>(db).findBy(
            Criteria(
              drogon_model::oauth2_db::WebauthnCredentials::Cols::_user_id,
              CompareOperator::EQ,
              resolvedId
            ),
            [sharedCb](const std::vector<drogon_model::oauth2_db::WebauthnCredentials> &creds) {
                Json::Value json;
                Json::Value credsJson(Json::arrayValue);
                for (const auto &wc : creds)
                {
                    Json::Value cred;
                    cred["credential_id"] = wc.getValueOfCredentialId();
                    auto n = wc.getName();
                    cred["name"] = n ? *n : "";
                    cred["sign_count"] = wc.getValueOfSignCount();
                    credsJson.append(cred);
                }
                json["credentials"] = credsJson;
                json["total"] = static_cast<int>(creds.size());
                (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
            },
            [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req,
                  sharedCb,
                  "DB_QUERY_ERROR",
                  std::string("listCredentials: query failed: ") + e.base().what()
                );
            }
          );
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req,
            sharedCb,
            "DB_QUERY_ERROR",
            std::string("listCredentials: user lookup failed: ") + e.base().what()
          );
      }
    );
}

}  // namespace authforge::drogon::controllers
