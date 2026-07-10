#include <authforge/drogon/controllers/SessionController.h>

#include <authforge/drogon/AuthService.h>
#include <authforge/drogon/controllers/EmailVerificationController.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <oauth2/observability/OAuth2Metrics.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <oauth2/utils/JwkManager.h>
#include <oauth2/observability/AuditLogger.h>
#include <drogon/utils/Utilities.h>
#include <algorithm>
#include <functional>
#include <oauth2/observability/openapi/OpenApiGenerator.h>
#include <oauth2/validation/RuleSet.h>
#include <oauth2/validation/HttpResponder.h>
#include <oauth2/types/OAuth2Types.h>
#include <oauth2/storage/IOAuth2Storage.h>
#include <oauth2/error/ErrorResponder.h>

using namespace oauth2;
using namespace authforge::drogon::services;
using namespace ::oauth2::observability::openapi;

namespace authforge::drogon::controllers
{

OAuth2Plugin *SessionController::resolvePlugin() const
{
    return plugin_ ? plugin_ : ::drogon::app().getPlugin<OAuth2Plugin>();
}

namespace
{
// Emit an Application error via the unified ErrorResponder entry point so the
// response body is always an Error Envelope (Requirement 7.1 / 7.3 / 7.5). The
// callback is taken by value so callers that have already moved their callback
// into an enclosing lambda can pass a copy.
void respondError(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> cb,
  std::string code,
  std::string detailForLog = ""
)
{
    ::common::error::ErrorResponder::respond(
      req,
      [cb = std::move(cb)](const ::drogon::HttpResponsePtr &r) { cb(r); },
      std::move(code),
      std::move(detailForLog)
    );
}
}  // namespace

}  // namespace authforge::drogon::controllers

static void sendBackchannelLogoutNotifications(const std::string &)
{
    LOG_DEBUG << "sendBackchannelLogoutNotifications: stub";
}

namespace authforge::drogon::controllers
{

// API documentation initialization
namespace
{
struct OAuth2ControllerDocs
{
    OAuth2ControllerDocs()
    {
        // Health endpoint
        {
            Json::Value successExample;
            successExample["status"] = "ok";
            successExample["version"] = "1.0.0";

            ::oauth2::observability::openapi::EndpointInfo healthEndpoint;
            healthEndpoint.path = "/health";
            healthEndpoint.method = "GET";
            healthEndpoint.summary = "Health check";
            healthEndpoint.description = "Returns the health status of the service.";
            healthEndpoint.tags = {"System"};
            healthEndpoint.parameters = {};
            healthEndpoint.responses = {{200, "Service is healthy"}};
            healthEndpoint.responseExamples = {{200, successExample}};
            healthEndpoint.requiresAuth = false;
            OpenApiGenerator::addEndpoint(healthEndpoint);
        }

        // Login endpoint
        {
            Json::Value successExample;
            successExample["status"] = "success";
            successExample["redirect_uri"] = "http://127.0.0.1:5173/callback?code=xyz123";

            Json::Value errorExample;
            errorExample["error"] = "invalid_client";

            ::oauth2::observability::openapi::EndpointInfo loginEndpoint;
            loginEndpoint.path = "/oauth2/login";
            loginEndpoint.method = "POST";
            loginEndpoint.summary = "Authenticate user";
            loginEndpoint.description =
              "Authenticates user credentials and generates an authorization code. "
              "Usually called by the frontend login page during the authorization code flow.";
            loginEndpoint.tags = {"OAuth2", "Authentication"};

            ::oauth2::observability::openapi::ParameterInfo usernameParam;
            usernameParam.name = "username";
            usernameParam.description = "User's account username (required)";
            usernameParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            usernameParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            usernameParam.required = true;

            ::oauth2::observability::openapi::ParameterInfo passwordParam;
            passwordParam.name = "password";
            passwordParam.description = "User's password (required)";
            passwordParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            passwordParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            passwordParam.required = true;

            ::oauth2::observability::openapi::ParameterInfo clientIdParam;
            clientIdParam.name = "client_id";
            clientIdParam.description = "Client identifier matches the requesting app (required)";
            clientIdParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            clientIdParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            clientIdParam.required = true;

            ::oauth2::observability::openapi::ParameterInfo redirectUriParam;
            redirectUriParam.name = "redirect_uri";
            redirectUriParam.description = "Redirect URI matching the registered client (required)";
            redirectUriParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            redirectUriParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            redirectUriParam.required = true;

            ::oauth2::observability::openapi::ParameterInfo scopeParam;
            scopeParam.name = "scope";
            scopeParam.description = "Requested scope, space-separated (optional)";
            scopeParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            scopeParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            scopeParam.required = false;

            ::oauth2::observability::openapi::ParameterInfo stateParam;
            stateParam.name = "state";
            stateParam.description = "Opaque value to maintain state (recommended)";
            stateParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            stateParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            stateParam.required = false;

            loginEndpoint.parameters = {
              usernameParam, passwordParam, clientIdParam, redirectUriParam, scopeParam, stateParam
            };
            loginEndpoint.responses =
              {{200, "Authentication successful (JSON with redirect_uri)"},
               {302, "Redirect with authorization code (if requested via browser)"},
               {401, "Authentication failed"}};
            loginEndpoint.responseExamples = {{200, successExample}, {401, errorExample}};
            loginEndpoint.requiresAuth = false;
            OpenApiGenerator::addEndpoint(loginEndpoint);
        }

        // Register endpoint
        {
            Json::Value successExample;
            successExample["status"] = "success";
            successExample["message"] = "User registered successfully";

            ::oauth2::observability::openapi::EndpointInfo registerEndpoint;
            registerEndpoint.path = "/api/register";
            registerEndpoint.method = "POST";
            registerEndpoint.summary = "Register new user";
            registerEndpoint.description = "Registers a new user account into the system.";
            registerEndpoint.tags = {"User", "Registration"};

            ::oauth2::observability::openapi::ParameterInfo usernameParam;
            usernameParam.name = "username";
            usernameParam.description = "Desired username (required)";
            usernameParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            usernameParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            usernameParam.required = true;

            ::oauth2::observability::openapi::ParameterInfo passwordParam;
            passwordParam.name = "password";
            passwordParam.description = "Strong password (required)";
            passwordParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            passwordParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            passwordParam.required = true;

            ::oauth2::observability::openapi::ParameterInfo emailParam;
            emailParam.name = "email";
            emailParam.description = "Email address (optional)";
            emailParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            emailParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            emailParam.required = false;

            registerEndpoint.parameters = {usernameParam, passwordParam, emailParam};
            registerEndpoint.responses =
              {{200, "User registered successfully"}, {400, "Invalid registration data"}};
            registerEndpoint.responseExamples = {{200, successExample}};
            registerEndpoint.requiresAuth = false;
            OpenApiGenerator::addEndpoint(registerEndpoint);
        }

        // Consent endpoint
        {
            ::oauth2::observability::openapi::EndpointInfo consentEndpoint;
            consentEndpoint.path = "/oauth2/consent";
            consentEndpoint.method = "POST";
            consentEndpoint.summary = "Submit user consent";
            consentEndpoint.description =
              "Submit user consent for requested scopes. Redirects back to client.";
            consentEndpoint.tags = {"OAuth2", "Consent"};

            ::oauth2::observability::openapi::ParameterInfo clientIdParam;
            clientIdParam.name = "client_id";
            clientIdParam.description = "Client identifier (required)";
            clientIdParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            clientIdParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            clientIdParam.required = true;

            ::oauth2::observability::openapi::ParameterInfo userIdParam;
            userIdParam.name = "user_id";
            userIdParam.description = "User identifier (required)";
            userIdParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            userIdParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            userIdParam.required = true;

            ::oauth2::observability::openapi::ParameterInfo scopeParam;
            scopeParam.name = "scope";
            scopeParam.description = "Requested scope to consent (required)";
            scopeParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            scopeParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            scopeParam.required = true;

            ::oauth2::observability::openapi::ParameterInfo redirectUriParam;
            redirectUriParam.name = "redirect_uri";
            redirectUriParam.description = "Redirect URI (required)";
            redirectUriParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            redirectUriParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            redirectUriParam.required = true;

            ::oauth2::observability::openapi::ParameterInfo stateParam;
            stateParam.name = "state";
            stateParam.description = "Opaque value to maintain state";
            stateParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            stateParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            stateParam.required = false;

            ::oauth2::observability::openapi::ParameterInfo actionParam;
            actionParam.name = "action";
            actionParam.description = "Action to perform: 'approve' or 'deny' (required)";
            actionParam.type = ::oauth2::observability::openapi::ParameterType::STRING;
            actionParam.location = ::oauth2::observability::openapi::ParameterLocation::QUERY;
            actionParam.required = true;
            actionParam.enumValues = "approve,deny";

            consentEndpoint.parameters =
              {clientIdParam, userIdParam, scopeParam, redirectUriParam, stateParam, actionParam};
            consentEndpoint.responses = {
              {302, "Redirect to client with authorization code or error"}
            };
            consentEndpoint.requiresAuth = false;
            OpenApiGenerator::addEndpoint(consentEndpoint);
        }
    }
};

OAuth2ControllerDocs docs_;
}  // namespace

void SessionController::showLoginPage(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    // Get OAuth2 parameters from URL
    auto params = req->getParameters();
    std::string clientId = params["client_id"];
    std::string redirectUri = params["redirect_uri"];
    std::string scope = params["scope"];
    std::string state = params["state"];
    std::string responseType = params["response_type"];
    std::string codeChallenge = params["code_challenge"];
    std::string codeChallengeMethod = params["code_challenge_method"];

    LOG_INFO << "Showing login page with OAuth2 parameters: client_id=" << clientId
             << ", code_challenge=" << (codeChallenge.empty() ? "not provided" : "provided");

    // Build frontend register URL from config
    std::string frontendRegisterUrl;
    auto customConfig = ::drogon::app().getCustomConfig();
    if (customConfig.isMember("frontend"))
    {
        const auto &frontend = customConfig["frontend"];
        std::string baseUrl = frontend.get("url", "http://localhost:5173").asString();
        std::string registerPath = frontend.get("register_path", "/register").asString();
        frontendRegisterUrl = baseUrl + registerPath;
    }
    else
    {
        frontendRegisterUrl = "http://localhost:5173/register";
    }

    // Create template data
    ::drogon::DrTemplateData data;
    data["client_id"] = clientId;
    data["redirect_uri"] = redirectUri;
    data["scope"] = scope;
    data["state"] = state;
    data["response_type"] = responseType;
    data["code_challenge"] = codeChallenge;
    data["code_challenge_method"] = codeChallengeMethod.empty() ? "plain" : codeChallengeMethod;
    data["frontend_register_url"] = frontendRegisterUrl;

    // Render login.csp template
    try
    {
        auto resp = ::drogon::HttpResponse::newHttpViewResponse("login", data);
        callback(resp);
    }
    catch (const std::exception &e)
    {
        respondError(
          req,
          std::move(callback),
          "INTERNAL_ERROR",
          std::string("Failed to render login page: ") + e.what()
        );
    }
}

void SessionController::login(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    // Use ValidatorHelper for consistent validation
    auto errors = ::oauth2::validation::RuleSet::login(req);

    // Return validation errors if any
    if (::oauth2::validation::HttpResponder::respondIfErrors(errors, std::move(callback)))
    {
        ::oauth2::observability::Metrics::incLoginFailure("validation_failed");
        return;
    }

    // Prefer POST body (JSON or form data) over URL parameters for security
    std::string username, password;
    std::string clientId, redirectUri, scope, state;
    std::string codeChallenge, codeChallengeMethod;
    std::string nonce;

    // Try JSON body first
    if (req->contentType() == ::drogon::CT_APPLICATION_JSON)
    {
        auto json = req->getJsonObject();
        if (json)
        {
            username = json->get("username", "").asString();
            password = json->get("password", "").asString();
            clientId = json->get("client_id", "").asString();
            redirectUri = json->get("redirect_uri", "").asString();
            scope = json->get("scope", "").asString();
            state = json->get("state", "").asString();
            codeChallenge = json->get("code_challenge", "").asString();
            codeChallengeMethod = json->get("code_challenge_method", "").asString();
            nonce = json->get("nonce", "").asString();
        }
    }
    // Fallback to form data (Drogon automatically parses form-urlencoded)
    else
    {
        auto params = req->getParameters();
        username = params["username"];
        password = params["password"];
        clientId = params["client_id"];
        redirectUri = params["redirect_uri"];
        scope = params["scope"];
        state = params["state"];
        codeChallenge = params["code_challenge"];
        codeChallengeMethod = params["code_challenge_method"];
        nonce = params["nonce"];
    }

    AuthService::validateUser(
      username,
      password,
      [this,
       req,
       username,
       clientId,
       scope,
       redirectUri,
       state,
       nonce,
       codeChallenge,
       codeChallengeMethod,
       callback = std::move(callback)](std::optional<services::AuthResult> authResult) mutable {
          if (authResult)
          {
              req->session()->insert("userId", std::to_string(authResult->internalId));

              // Audit: login success
              ::oauth2::observability::AuditLogger::log(
                "login_success",
                "success",
                req,
                authResult->publicSub,
                "user",
                authResult->publicSub
              );

              // === CHECK 1: Email verification enforcement ===
              auto customCfg = ::drogon::app().getCustomConfig();
              bool requireEmailVerification = false;
              if (
                customCfg.isMember("auth") &&
                customCfg["auth"].isMember("require_email_verification")
              )
              {
                  requireEmailVerification =
                    customCfg["auth"]["require_email_verification"].asBool();
              }
              if (requireEmailVerification && !authResult->emailVerified)
              {
                  respondError(
                    req, std::move(callback), "AUTHZ_ACCESS_DENIED", "login: email not verified"
                  );
                  return;
              }

              // === CHECK 2: MFA enforcement ===
              if (authResult->mfaEnabled)
              {
                  auto internalId = authResult->internalId;
                  auto sharedCb = std::make_shared<
                    std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
                  auto db = ::drogon::app().getDbClient();
                  db->execSqlAsync(
                    "UPDATE users SET mfa_pending_client_id = $1, "
                    "mfa_pending_redirect_uri = $2 WHERE id = $3",
                    [req, internalId, sharedCb](const ::drogon::orm::Result &) {
                        Json::Value mfaResp;
                        mfaResp["mfa_required"] = true;
                        mfaResp["mfa_token"] = std::to_string(internalId);
                        mfaResp["message"] =
                          "MFA verification required. Submit TOTP code to "
                          "/oauth2/mfa/verify";
                        auto resp = ::drogon::HttpResponse::newHttpJsonResponse(mfaResp);
                        resp->setStatusCode(::drogon::k200OK);
                        (*sharedCb)(resp);
                    },
                    [req, sharedCb](const ::drogon::orm::DrogonDbException &e) {
                        respondError(
                          req,
                          *sharedCb,
                          "DB_QUERY_ERROR",
                          std::string("login: failed to persist MFA pending "
                                      "binding: ") +
                            e.base().what()
                        );
                    },
                    clientId,
                    redirectUri,
                    internalId
                  );
                  return;
              }

              // === CHECK 3: PKCE enforcement for PUBLIC clients ===
              bool requirePkce = false;
              if (
                customCfg.isMember("auth") && customCfg["auth"].isMember("require_pkce_for_public")
              )
              {
                  requirePkce = customCfg["auth"]["require_pkce_for_public"].asBool();
              }
              if (requirePkce && codeChallenge.empty())
              {
                  LOG_WARN << "[SECURITY] PUBLIC client " << clientId
                           << " login without PKCE (enforcement enabled)";
                  respondError(
                    req,
                    std::move(callback),
                    "VALIDATION_MISSING_REQUIRED_FIELD",
                    "login: PKCE (code_challenge) is required for public clients"
                  );
                  return;
              }

              auto plugin = resolvePlugin();
              if (!plugin)
              {
                  respondError(
                    req, std::move(callback), "INTERNAL_ERROR", "login: OAuth2 Plugin not loaded"
                  );
                  return;
              }

              plugin->generateAuthorizationCode(
                clientId,
                authResult->publicSub,
                scope,
                redirectUri,
                codeChallenge,
                codeChallengeMethod,
                nonce,
                [req,
                 redirectUri,
                 state,
                 codeChallenge,
                 codeChallengeMethod,
                 callback =
                   std::move(callback)](bool success, std::string code, std::string error) mutable {
                    if (!success)
                    {
                        respondError(
                          req,
                          std::move(callback),
                          "INTERNAL_ERROR",
                          "login: failed to generate authorization code: " + error
                        );
                        return;
                    }

                    std::string location = redirectUri + "?code=" + code;
                    if (!state.empty())
                        location += "&state=" + state;
                    if (req->getParameter("json") == "true")
                    {
                        Json::Value ret;
                        ret["code"] = code;
                        ret["location"] = location;
                        auto resp = ::drogon::HttpResponse::newHttpJsonResponse(ret);
                        callback(resp);
                        return;
                    }
                    auto resp = ::drogon::HttpResponse::newRedirectionResponse(location);
                    callback(resp);
                }
              );
          }
          else
          {
              // Fail (Bad Password or User Not Found)
              ::oauth2::observability::Metrics::incLoginFailure("bad_credentials");

              // Audit: login failure
              ::oauth2::observability::AuditLogger::log(
                "login_failure", "failure", req, username, "user", username
              );

              respondError(
                req, std::move(callback), "AUTH_INVALID_CREDENTIALS", "login: invalid credentials"
              );
          }
      }
    );
}

void SessionController::consent(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    // P0-2: Handle user consent approval
    auto params = req->getParameters();
    std::string clientId = params["client_id"];
    std::string userId = params["user_id"];
    std::string scope = params["scope"];
    std::string redirectUri = params["redirect_uri"];
    std::string state = params["state"];
    std::string action = params["action"];  // "approve" or "deny"
    std::string codeChallenge = params["code_challenge"];
    std::string codeChallengeMethod = params["code_challenge_method"];
    std::string nonce = params["nonce"];

    if (action == "deny")
    {
        std::string location =
          redirectUri + "?error=access_denied&error_description=User+denied+consent";
        if (!state.empty())
            location += "&state=" + state;
        auto resp = ::drogon::HttpResponse::newRedirectionResponse(location);
        callback(resp);
        return;
    }

    auto plugin = resolvePlugin();
    if (!plugin)
    {
        respondError(
          req, std::move(callback), "INTERNAL_ERROR", "consent: OAuth2 Plugin not loaded"
        );
        return;
    }

    plugin->getInternalUserId(
      userId,
      [plugin,
       clientId,
       userId,
       scope,
       redirectUri,
       state,
       codeChallenge,
       codeChallengeMethod,
       nonce,
       req,
       callback = std::move(callback)](std::optional<int32_t> internalUserId) mutable {
          if (!internalUserId)
          {
              respondError(
                req, std::move(callback), "INTERNAL_ERROR", "consent: failed to get user mapping"
              );
              return;
          }

          std::vector<std::string> scopes;
          std::stringstream ss(scope);
          std::string scopeItem;
          while (std::getline(ss, scopeItem, ' '))
          {
              if (!scopeItem.empty())
              {
                  scopes.push_back(scopeItem);
              }
          }

          if (!scopes.empty())
          {
              std::string firstScope = scopes[0];
              int32_t uid = *internalUserId;
              plugin->saveUserConsent(
                uid,
                clientId,
                firstScope,
                [plugin,
                 uid,
                 clientId,
                 userId,
                 scope,
                 redirectUri,
                 state,
                 codeChallenge,
                 codeChallengeMethod,
                 nonce,
                 firstScope,
                 scopes,
                 req,
                 callback = std::move(callback)](bool success) mutable {
                    if (!success)
                    {
                        respondError(
                          req,
                          std::move(callback),
                          "INTERNAL_ERROR",
                          "consent: failed to save user consent for scope: " + firstScope
                        );
                        return;
                    }

                    for (size_t i = 1; i < scopes.size(); ++i)
                    {
                        plugin->saveUserConsent(uid, clientId, scopes[i], [](bool) {});
                    }

                    plugin->generateAuthorizationCode(
                      clientId,
                      userId,
                      scope,
                      redirectUri,
                      codeChallenge,
                      codeChallengeMethod,
                      nonce,
                      [clientId, redirectUri, state, req, callback = std::move(callback)](
                        bool success, std::string code, std::string error
                      ) mutable {
                          if (!success)
                          {
                              respondError(
                                req,
                                std::move(callback),
                                "INTERNAL_ERROR",
                                "consent: failed to generate authorization code: " + error
                              );
                              return;
                          }

                          std::string location = redirectUri + "?code=" + code;
                          if (!state.empty())
                              location += "&state=" + state;
                          auto resp = ::drogon::HttpResponse::newRedirectionResponse(location);
                          ::oauth2::observability::Metrics::incRequest("authorize", 302);
                          callback(resp);
                      }
                    );
                }
              );
          }
          else
          {
              plugin->generateAuthorizationCode(
                clientId,
                userId,
                scope,
                redirectUri,
                codeChallenge,
                codeChallengeMethod,
                nonce,
                [clientId, redirectUri, state, req, callback = std::move(callback)](
                  bool success, std::string code, std::string error
                ) mutable {
                    if (!success)
                    {
                        respondError(
                          req,
                          std::move(callback),
                          "INTERNAL_ERROR",
                          "consent: failed to generate authorization code: " + error
                        );
                        return;
                    }

                    std::string location = redirectUri + "?code=" + code;
                    if (!state.empty())
                        location += "&state=" + state;
                    auto resp = ::drogon::HttpResponse::newRedirectionResponse(location);
                    ::oauth2::observability::Metrics::incRequest("authorize", 302);
                    callback(resp);
                }
              );
          }
      }
    );
}

void SessionController::logout(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    // Check Authorization header (OAuth2Middleware normally handles this,
    // but direct calls in tests may bypass the filter)
    auto authHeader = req->getHeader("Authorization");
    if (authHeader.empty() || authHeader.length() < 8 || authHeader.substr(0, 7) != "Bearer ")
    {
        respondError(
          req,
          std::move(callback),
          "AUTH_TOKEN_INVALID",
          "logout: missing or invalid Authorization header"
        );
        return;
    }

    // The OAuth2Middleware filter has already validated the token and set attributes
    auto attrs = req->getAttributes();
    std::string userId = attrs->get<std::string>("userId");
    std::string clientId = attrs->get<std::string>("clientId");

    std::string token = authHeader.substr(7);  // Remove "Bearer "

    auto plugin = resolvePlugin();
    if (!plugin)
    {
        respondError(
          req, std::move(callback), "INTERNAL_ERROR", "logout: OAuth2 Plugin not loaded"
        );
        return;
    }

    // Revoke the access token
    plugin->revokeAccessToken(token, clientId, [userId, callback = std::move(callback)]() mutable {
        LOG_INFO << "Logout: Token revoked for user " << userId;

        // Send backchannel logout notifications (fire-and-forget)
        sendBackchannelLogoutNotifications(userId);

        // Respond immediately without waiting for backchannel notifications
        Json::Value json;
        json["message"] = "Logged out successfully";
        auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(::drogon::k200OK);
        callback(resp);
    });
}

void SessionController::registerUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto errors = ::oauth2::validation::RuleSet::registerUser(req);
    if (::oauth2::validation::HttpResponder::respondIfErrors(errors, std::move(callback)))
        return;
    // Parse the same fields RuleSet::registerUser validated. Duplicated inline
    // (not shared with RuleSet) by decision: getParameters() returns empty for
    // application/json bodies, which previously persisted bogus accounts.
    std::string username, password, email;
    if (req->contentType() == ::drogon::CT_APPLICATION_JSON)
    {
        auto json = req->getJsonObject();
        if (json)
        {
            username = json->get("username", "").asString();
            password = json->get("password", "").asString();
            email = json->get("email", "").asString();
        }
    }
    else
    {
        auto params = req->getParameters();
        username = params["username"];
        password = params["password"];
        email = params["email"];
    }

    AuthService::registerUser(
      username, password, email, [callback, email, req](const std::string &errorCode) {
          if (errorCode.empty())
          {
              Json::Value json;
              json["message"] = "User registered successfully";
              if (!email.empty())
                  json["note"] = "Please check your email to verify your account";
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              callback(resp);
          }
          else
          {
              // Forward the structured Error_Code from AuthService verbatim to
              // ErrorResponder — no text inspection or hardcoded fallback
              // (Requirement 1.6).
              respondError(
                req, callback, errorCode, "registerUser failed: " + errorCode
              );
          }
      }
    );
}

}  // namespace authforge::drogon::controllers
