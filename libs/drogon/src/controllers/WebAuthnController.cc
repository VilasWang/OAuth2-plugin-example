#include <authforge/drogon/controllers/WebAuthnController.h>
#include <oauth2/utils/CryptoUtils.h>
#include <oauth2/observability/AuditLogger.h>
#include <oauth2/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <chrono>

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
    ::common::error::ErrorResponder::respond(
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
        ::oauth2::observability::openapi::EndpointInfo regBeginDocs;
        regBeginDocs.path = "/oauth2/webauthn/register/begin";
        regBeginDocs.method = "POST";
        regBeginDocs.summary = "WebAuthn Register Begin";
        regBeginDocs.description = "Start WebAuthn registration.";
        regBeginDocs.tags = {"WebAuthn"};
        regBeginDocs.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(regBeginDocs);

        ::oauth2::observability::openapi::EndpointInfo regFinishDocs;
        regFinishDocs.path = "/oauth2/webauthn/register/finish";
        regFinishDocs.method = "POST";
        regFinishDocs.summary = "WebAuthn Register Finish";
        regFinishDocs.description = "Finish WebAuthn registration.";
        regFinishDocs.tags = {"WebAuthn"};
        regFinishDocs.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(regFinishDocs);

        ::oauth2::observability::openapi::EndpointInfo loginBeginDocs;
        loginBeginDocs.path = "/oauth2/webauthn/login/begin";
        loginBeginDocs.method = "POST";
        loginBeginDocs.summary = "WebAuthn Login Begin";
        loginBeginDocs.description = "Start WebAuthn login.";
        loginBeginDocs.tags = {"WebAuthn"};
        loginBeginDocs.requiresAuth = false;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(loginBeginDocs);

        ::oauth2::observability::openapi::EndpointInfo loginFinishDocs;
        loginFinishDocs.path = "/oauth2/webauthn/login/finish";
        loginFinishDocs.method = "POST";
        loginFinishDocs.summary = "WebAuthn Login Finish";
        loginFinishDocs.description = "Finish WebAuthn login.";
        loginFinishDocs.tags = {"WebAuthn"};
        loginFinishDocs.requiresAuth = false;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(loginFinishDocs);

        ::oauth2::observability::openapi::EndpointInfo credentialsDocs;
        credentialsDocs.path = "/oauth2/webauthn/credentials";
        credentialsDocs.method = "GET";
        credentialsDocs.summary = "List WebAuthn Credentials";
        credentialsDocs.description = "List registered WebAuthn credentials.";
        credentialsDocs.tags = {"WebAuthn"};
        credentialsDocs.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(credentialsDocs);
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

    // Generate challenge (32 bytes, base64url encoded)
    std::string challenge = ::oauth2::utils::generateSecureToken();

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

    // Store credential
    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "INSERT INTO webauthn_credentials (user_id, credential_id, public_key, name) "
      "VALUES ((SELECT id FROM users WHERE public_sub::text = $1::text), $2, $3, $4)",
      [sharedCb, credentialId, req, userId](const ::drogon::orm::Result &) {
          ::oauth2::observability::AuditLogger::log(
            "webauthn_registered", "success", req, userId, "credential", credentialId
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
          if (what.find("webauthn_credentials") != std::string::npos &&
              what.find("credential_id") != std::string::npos)
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
      },
      userId,
      credentialId,
      publicKey,
      credName
    );
}

void WebAuthnController::authenticateBegin(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    // Generate challenge
    std::string challenge = ::oauth2::utils::generateSecureToken();

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

    // Look up credential and verify
    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "SELECT wc.user_id, wc.sign_count, u.public_sub "
      "FROM webauthn_credentials wc "
      "JOIN users u ON wc.user_id = u.id "
      "WHERE wc.credential_id = $1",
      [sharedCb, credentialId, db, req](const ::drogon::orm::Result &r) {
          if (r.empty())
          {
              respondError(
                req,
                sharedCb,
                "AUTH_INVALID_CREDENTIALS",
                "authenticateFinish: credential not found"
              );
              return;
          }

          int userId = r[0]["user_id"].as<int>();
          std::string publicSub = r[0]["public_sub"].as<std::string>();
          int signCount = r[0]["sign_count"].as<int>();

          // Update sign_count and last_used_at
          db->execSqlAsync(
            "UPDATE webauthn_credentials SET sign_count = $1, last_used_at = NOW() "
            "WHERE credential_id = $2",
            [](const ::drogon::orm::Result &) {},
            [](const ::drogon::orm::DrogonDbException &) {},
            signCount + 1,
            credentialId
          );

          ::oauth2::observability::AuditLogger::log(
            "webauthn_authenticated", "success", req, publicSub, "credential", credentialId
          );

          // Return success with user info (caller can then issue tokens)
          Json::Value json;
          json["authenticated"] = true;
          json["user_id"] = publicSub;
          json["sign_count"] = signCount + 1;
          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
          (*sharedCb)(resp);
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req,
            sharedCb,
            "DB_QUERY_ERROR",
            std::string("authenticateFinish: lookup failed: ") + e.base().what()
          );
      },
      credentialId
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

    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "SELECT credential_id, name, sign_count, created_at, last_used_at "
      "FROM webauthn_credentials "
      "WHERE user_id = (SELECT id FROM users WHERE public_sub::text = $1::text) "
      "ORDER BY created_at DESC",
      [sharedCb](const ::drogon::orm::Result &r) {
          Json::Value json;
          Json::Value creds(Json::arrayValue);
          for (const auto &row : r)
          {
              Json::Value cred;
              cred["credential_id"] = row["credential_id"].as<std::string>();
              cred["name"] = row["name"].isNull() ? "" : row["name"].as<std::string>();
              cred["sign_count"] = row["sign_count"].as<int>();
              creds.append(cred);
          }
          json["credentials"] = creds;
          json["total"] = static_cast<int>(r.size());
          (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req,
            sharedCb,
            "DB_QUERY_ERROR",
            std::string("listCredentials: query failed: ") + e.base().what()
          );
      },
      userId
    );
}

}  // namespace authforge::drogon::controllers
