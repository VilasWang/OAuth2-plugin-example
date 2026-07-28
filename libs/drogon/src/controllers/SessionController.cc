#include <authforge/drogon/controllers/SessionController.h>
#include <authforge/storage/postgres/models/Users.h>
#include <authforge/drogon/adapters/DrogonAuditSink.h>

#include <authforge/drogon/AuthService.h>
#include <authforge/drogon/controllers/EmailVerificationController.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <authforge/oauth2/jwk/JwkManager.h>
#include <drogon/utils/Utilities.h>
#include <algorithm>
#include <functional>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <authforge/drogon/validation/RuleSet.h>
#include <authforge/drogon/validation/HttpResponder.h>
#include <authforge/drogon/error/ErrorResponder.h>

// Task 24 slice 4 (authforge-sdk-refactor): identity-layer services this
// controller now optionally consumes (see SessionController.h's
// setIdentityAuthService()/setSessionManager() comments for the
// wiring/fallback contract).
#include <authforge/identity/AuthService.h>
#include <authforge/identity/SessionManager.h>

// Phase 1.5d (Task 39): `using namespace oauth2;` was removed -- this TU
// has no actual oauth2:: symbol references (all calls are fully qualified
// ::authforge::...). The directive previously compiled only because
// OAuth2Plugin.h transitively pulled in `namespace oauth2` via the legacy
// oauth2/storage/I*Repository.h headers, which the plugin no longer includes.
using namespace authforge::drogon::services;
using namespace ::authforge::drogon::observability::openapi;

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
    ::authforge::common::error::ErrorResponder::respond(
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

using namespace ::drogon::orm;
using namespace ::drogon_model::oauth2_db;

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

            ::authforge::drogon::observability::openapi::EndpointInfo healthEndpoint;
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

            ::authforge::drogon::observability::openapi::EndpointInfo loginEndpoint;
            loginEndpoint.path = "/oauth2/login";
            loginEndpoint.method = "POST";
            loginEndpoint.summary = "Authenticate user";
            loginEndpoint.description =
              "Authenticates user credentials and generates an authorization code. "
              "Usually called by the frontend login page during the authorization code flow.";
            loginEndpoint.tags = {"OAuth2", "Authentication"};

            ::authforge::drogon::observability::openapi::ParameterInfo usernameParam;
            usernameParam.name = "username";
            usernameParam.description = "User's account username (required)";
            usernameParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            usernameParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
            usernameParam.required = true;

            ::authforge::drogon::observability::openapi::ParameterInfo passwordParam;
            passwordParam.name = "password";
            passwordParam.description = "User's password (required)";
            passwordParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            passwordParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
            passwordParam.required = true;

            ::authforge::drogon::observability::openapi::ParameterInfo clientIdParam;
            clientIdParam.name = "client_id";
            clientIdParam.description = "Client identifier matches the requesting app (required)";
            clientIdParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            clientIdParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
            clientIdParam.required = true;

            ::authforge::drogon::observability::openapi::ParameterInfo redirectUriParam;
            redirectUriParam.name = "redirect_uri";
            redirectUriParam.description = "Redirect URI matching the registered client (required)";
            redirectUriParam.type =
              ::authforge::drogon::observability::openapi::ParameterType::STRING;
            redirectUriParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
            redirectUriParam.required = true;

            ::authforge::drogon::observability::openapi::ParameterInfo scopeParam;
            scopeParam.name = "scope";
            scopeParam.description = "Requested scope, space-separated (optional)";
            scopeParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            scopeParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
            scopeParam.required = false;

            ::authforge::drogon::observability::openapi::ParameterInfo stateParam;
            stateParam.name = "state";
            stateParam.description = "Opaque value to maintain state (recommended)";
            stateParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            stateParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
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

            ::authforge::drogon::observability::openapi::EndpointInfo registerEndpoint;
            registerEndpoint.path = "/api/register";
            registerEndpoint.method = "POST";
            registerEndpoint.summary = "Register new user";
            registerEndpoint.description = "Registers a new user account into the system.";
            registerEndpoint.tags = {"User", "Registration"};

            ::authforge::drogon::observability::openapi::ParameterInfo usernameParam;
            usernameParam.name = "username";
            usernameParam.description = "Desired username (required)";
            usernameParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            usernameParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
            usernameParam.required = true;

            ::authforge::drogon::observability::openapi::ParameterInfo passwordParam;
            passwordParam.name = "password";
            passwordParam.description = "Strong password (required)";
            passwordParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            passwordParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
            passwordParam.required = true;

            ::authforge::drogon::observability::openapi::ParameterInfo emailParam;
            emailParam.name = "email";
            emailParam.description = "Email address (optional)";
            emailParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            emailParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
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
            ::authforge::drogon::observability::openapi::EndpointInfo consentEndpoint;
            consentEndpoint.path = "/oauth2/consent";
            consentEndpoint.method = "POST";
            consentEndpoint.summary = "Submit user consent";
            consentEndpoint.description =
              "Submit user consent for requested scopes. Redirects back to client.";
            consentEndpoint.tags = {"OAuth2", "Consent"};

            ::authforge::drogon::observability::openapi::ParameterInfo clientIdParam;
            clientIdParam.name = "client_id";
            clientIdParam.description = "Client identifier (required)";
            clientIdParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            clientIdParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
            clientIdParam.required = true;

            ::authforge::drogon::observability::openapi::ParameterInfo userIdParam;
            userIdParam.name = "user_id";
            userIdParam.description = "User identifier (required)";
            userIdParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            userIdParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
            userIdParam.required = true;

            ::authforge::drogon::observability::openapi::ParameterInfo scopeParam;
            scopeParam.name = "scope";
            scopeParam.description = "Requested scope to consent (required)";
            scopeParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            scopeParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
            scopeParam.required = true;

            ::authforge::drogon::observability::openapi::ParameterInfo redirectUriParam;
            redirectUriParam.name = "redirect_uri";
            redirectUriParam.description = "Redirect URI (required)";
            redirectUriParam.type =
              ::authforge::drogon::observability::openapi::ParameterType::STRING;
            redirectUriParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
            redirectUriParam.required = true;

            ::authforge::drogon::observability::openapi::ParameterInfo stateParam;
            stateParam.name = "state";
            stateParam.description = "Opaque value to maintain state";
            stateParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            stateParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
            stateParam.required = false;

            ::authforge::drogon::observability::openapi::ParameterInfo actionParam;
            actionParam.name = "action";
            actionParam.description = "Action to perform: 'approve' or 'deny' (required)";
            actionParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
            actionParam.location =
              ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
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
    auto errors = ::authforge::drogon::validation::RuleSet::login(req);

    // Return validation errors if any
    if (
      ::authforge::drogon::validation::HttpResponder::respondIfErrors(errors, std::move(callback))
    )
    {
        if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
            m->incrementCounter(
              "oauth2_login_failures_total",
              authforge::common::ports::MetricLabels{{"reason", "validation_failed"}}
            );
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

    // Task 24 slice 4 (authforge-sdk-refactor): validateUser's continuation
    // is identical regardless of which AuthService implementation ran it
    // (only the internalId width differs: authforge::identity::AuthResult
    // uses int64_t, the pre-Task-24 authforge::drogon::services::AuthResult
    // uses int). This shared continuation takes the widened representation
    // so both branches below (new identity::AuthService if injected, else
    // the legacy drogon::services::AuthService fallback) funnel into the
    // exact same logic -- no duplicated CHECK 1/2/3 chain to keep in sync.
    auto onValidated = [this,
                        req,
                        username,
                        clientId,
                        scope,
                        redirectUri,
                        state,
                        nonce,
                        codeChallenge,
                        codeChallengeMethod,
                        callback = std::move(callback)](
                         bool success,
                         int64_t internalId,
                         std::string publicSub,
                         bool emailVerified,
                         bool mfaEnabled
                       ) mutable {
        if (success)
        {
            req->session()->insert("userId", std::to_string(internalId));

            // Audit: login success
            ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
              ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
              "login_success",
              "success",
              req,
              publicSub,
              "user",
              publicSub
            );

            // === CHECK 1/2: email verification + MFA enforcement ===
            // Task 24 slice 4: delegates to
            // authforge::identity::evaluateLoginPolicy() (design.md §5.1/§6),
            // the pure-function extraction of this exact if/else chain
            // (email-verification precedence over MFA verified against
            // this file's own pre-Task-24 source, see SessionManager.h's
            // top comment) -- called unconditionally (not gated on
            // sessionManager_ being wired) because it is a stateless free
            // function, not an instance method requiring injected state.
            auto customCfg = ::drogon::app().getCustomConfig();
            bool requireEmailVerification = false;
            if (
              customCfg.isMember("auth") && customCfg["auth"].isMember("require_email_verification")
            )
            {
                requireEmailVerification = customCfg["auth"]["require_email_verification"].asBool();
            }

            authforge::identity::AuthResult policyInput;
            policyInput.internalId = internalId;
            policyInput.publicSub = publicSub;
            policyInput.emailVerified = emailVerified;
            policyInput.mfaEnabled = mfaEnabled;
            auto decision =
              authforge::identity::evaluateLoginPolicy(policyInput, requireEmailVerification);

            if (decision == authforge::identity::LoginDecision::DenyEmailNotVerified)
            {
                respondError(
                  req, std::move(callback), "AUTHZ_ACCESS_DENIED", "login: email not verified"
                );
                return;
            }

            if (decision == authforge::identity::LoginDecision::RequireMfa)
            {
                auto sharedCb =
                  std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
                    std::move(callback)
                  );
                auto db = ::drogon::app().getDbClient();
                // users.id is a Postgres `integer` (32-bit) column --
                // narrow explicitly so libpq binds this parameter as
                // int4, not int8/bigint (Task 24 slice 4: internalId is
                // int64_t here, widened to match
                // authforge::identity::AuthResult's type; binding an
                // 8-byte value against a `WHERE id = $3` integer column
                // fails at the wire-protocol/type-coercion level, not as
                // a SQL syntax error).
                int32_t internalIdInt32 = static_cast<int32_t>(internalId);
                // Task B5: replaced raw SQL with Mapper<Users>
                Criteria mfaCrit(Users::Cols::_id, CompareOperator::EQ, internalIdInt32);
                Mapper<Users>(db).findOne(
                  mfaCrit,
                  [req, internalId, sharedCb, db, clientId, redirectUri](const Users &user) {
                      Users mfaUpdated = user;
                      mfaUpdated.setMfaPendingClientId(clientId);
                      mfaUpdated.setMfaPendingRedirectUri(redirectUri);
                      Mapper<Users>(db).update(
                        mfaUpdated,
                        [req, internalId, sharedCb](const size_t) {
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
                        [req, sharedCb](const DrogonDbException &e) {
                            respondError(
                              req,
                              *sharedCb,
                              "DB_QUERY_ERROR",
                              std::string("login: failed to persist MFA pending binding: ") +
                                e.base().what()
                            );
                        }
                      );
                  },
                  [req, sharedCb](const DrogonDbException &e) {
                      respondError(
                        req,
                        *sharedCb,
                        "DB_QUERY_ERROR",
                        std::string("login: failed to persist MFA pending binding: ") +
                          e.base().what()
                      );
                  }
                );
                return;
            }

            // === CHECK 3: PKCE enforcement for PUBLIC clients ===
            bool requirePkce = false;
            if (customCfg.isMember("auth") && customCfg["auth"].isMember("require_pkce_for_public"))
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
              publicSub,
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
            if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                m->incrementCounter(
                  "oauth2_login_failures_total",
                  authforge::common::ports::MetricLabels{{"reason", "bad_credentials"}}
                );

            // Audit: login failure
            ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
              ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
              "login_failure",
              "failure",
              req,
              username,
              "user",
              username
            );

            respondError(
              req, std::move(callback), "AUTH_INVALID_CREDENTIALS", "login: invalid credentials"
            );
        }
    };

    // Task 24 slice 4: prefer the injected authforge::identity::AuthService
    // (constructed once at startup by
    // bootstrap::wireIdentityServices()/OAuth2Server/bootstrap/
    // IdentityAssembly.cc, backed by PostgresIdentityRepository +
    // OpenSslCryptoProvider + SystemClock -- see that file for the
    // construction site) when wired; otherwise fall back to the
    // pre-Task-24 authforge::drogon::services::AuthService (static,
    // Mapper<Users>-backed) so this controller keeps working unchanged in
    // any binary that has not called setIdentityAuthService() yet (e.g.
    // OAuth2Server/test/e2e's direct-construction tests, until they are
    // updated). Both AuthResult shapes are adapted into onValidated's
    // widened (bool, int64_t, string, bool, bool) signature above.
    if (identityAuthService_)
    {
        identityAuthService_->validateUser(
          username,
          password,
          [onValidated = std::move(onValidated)](
            std::optional<authforge::identity::AuthResult> result
          ) mutable {
              if (!result)
              {
                  onValidated(false, 0, "", false, false);
                  return;
              }
              onValidated(
                true,
                result->internalId,
                result->publicSub,
                result->emailVerified,
                result->mfaEnabled
              );
          }
        );
    }
    else
    {
        AuthService::validateUser(
          username,
          password,
          [onValidated =
             std::move(onValidated)](std::optional<services::AuthResult> result) mutable {
              if (!result)
              {
                  onValidated(false, 0, "", false, false);
                  return;
              }
              onValidated(
                true,
                static_cast<int64_t>(result->internalId),
                result->publicSub,
                result->emailVerified,
                result->mfaEnabled
              );
          }
        );
    }
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
                          if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                              m->incrementCounter(
                                "oauth2_requests_total",
                                authforge::common::ports::MetricLabels{{"endpoint", "authorize"}},
                                static_cast<double>(302)
                              );
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
                    if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                        m->incrementCounter(
                          "oauth2_requests_total",
                          authforge::common::ports::MetricLabels{{"endpoint", "authorize"}},
                          static_cast<double>(302)
                        );
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
    // Task 24 slice 4: prefer the injected authforge::identity::SessionManager
    // (its logout() forwards to a real IBackchannelLogoutNotifier
    // implementation, see bootstrap::wireIdentityServices()) over the
    // pre-Task-24 sendBackchannelLogoutNotifications() stub (which only
    // logs, see this file's static function above), falling back to that
    // stub when unset -- same injected-with-fallback pattern as
    // login()/registerUser() above.
    auto *sessionManager = sessionManager_;
    plugin->revokeAccessToken(
      token, clientId, [userId, sessionManager, callback = std::move(callback)]() mutable {
          LOG_INFO << "Logout: Token revoked for user " << userId;

          auto respond = [callback = std::move(callback)]() mutable {
              // Respond immediately without waiting for backchannel
              // notifications to fully complete on the caller's side --
              // both branches below still invoke this only after their
              // respective notify() completes (fire-and-forget from the
              // HTTP response's perspective, matching the pre-Task-24
              // stub's synchronous-log-then-respond timing).
              Json::Value json;
              json["message"] = "Logged out successfully";
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              resp->setStatusCode(::drogon::k200OK);
              callback(resp);
          };

          if (sessionManager)
              sessionManager->logout(userId, std::move(respond));
          else
          {
              sendBackchannelLogoutNotifications(userId);
              respond();
          }
      }
    );
}

void SessionController::registerUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto errors = ::authforge::drogon::validation::RuleSet::registerUser(req);
    if (
      ::authforge::drogon::validation::HttpResponder::respondIfErrors(errors, std::move(callback))
    )
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

    auto onRegistered = [callback, email, req](const std::string &errorCode) {
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
            respondError(req, callback, errorCode, "registerUser failed: " + errorCode);
        }
    };

    // Task 24 slice 4: same injected-service-with-fallback pattern as
    // login() above -- both AuthService::registerUser overloads share the
    // exact (const std::string &errorCode) callback contract already, so
    // onRegistered needs no adaptation.
    if (identityAuthService_)
        identityAuthService_->registerUser(username, password, email, onRegistered);
    else
        AuthService::registerUser(username, password, email, onRegistered);
}

}  // namespace authforge::drogon::controllers
