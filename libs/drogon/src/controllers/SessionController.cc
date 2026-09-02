#include <mutex>
#include <fulla/drogon/controllers/SessionController.h>
#include <fulla/drogon/utils/ConsentCsrfSlots.h>
#include <fulla/drogon/utils/PasswordHasher.h>
#include <fulla/storage/postgres/models/Users.h>
#include <fulla/drogon/adapters/DrogonAuditSink.h>

#include <fulla/drogon/AuthService.h>
#include <fulla/drogon/controllers/EmailVerificationController.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <fulla/drogon/plugin/OAuth2Plugin.h>
#include <fulla/oauth2/jwk/JwkManager.h>
#include <drogon/utils/Utilities.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <json/json.h>
#include <sstream>
#include <vector>
#include <fulla/drogon/observability/openapi/OpenApiGenerator.h>
#include <fulla/drogon/validation/RuleSet.h>
#include <fulla/drogon/validation/HttpResponder.h>
#include <fulla/drogon/error/ErrorResponder.h>

// Task 24 slice 4 (fulla-sdk-refactor): identity-layer services this
// controller now optionally consumes (see SessionController.h's
// setIdentityAuthService()/setSessionManager() comments for the
// wiring/fallback contract).
#include <fulla/identity/AuthService.h>
#include <fulla/identity/SessionManager.h>

// Phase 1.5d (Task 39): `using namespace oauth2;` was removed -- this TU
// has no actual oauth2:: symbol references (all calls are fully qualified
// ::fulla::...). The directive previously compiled only because
// OAuth2Plugin.h transitively pulled in `namespace oauth2` via the legacy
// oauth2/storage/I*Repository.h headers, which the plugin no longer includes.
using namespace fulla::drogon::services;
using namespace ::fulla::drogon::observability::openapi;

namespace fulla::drogon::controllers
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
    ::fulla::common::error::ErrorResponder::respond(
      req,
      [cb = std::move(cb)](const ::drogon::HttpResponsePtr &r) { cb(r); },
      std::move(code),
      std::move(detailForLog)
    );
}

// F-007 (RFC 6749 §4.1.2.1): errors raised while processing an
// authorization request are delivered as a 302 back to the client's
// (verified) redirect_uri with error/error_description/state in the query.
void sendOAuthErrorRedirect(
  const std::function<void(const ::drogon::HttpResponsePtr &)> &cb,
  const std::string &redirectUri,
  const std::string &error,
  const std::string &description,
  const std::string &state
)
{
    std::string location = redirectUri + "?error=" + error;
    if (!description.empty())
        location += "&error_description=" + ::drogon::utils::urlEncode(description);
    if (!state.empty())
        location += "&state=" + ::drogon::utils::urlEncode(state);
    cb(::drogon::HttpResponse::newRedirectionResponse(location));
}
}  // namespace

}  // namespace fulla::drogon::controllers

namespace fulla::drogon::controllers
{

namespace
{

// #78: stable short names for JwkManager::verifyJwt() rejection reasons, used
// as Internal_Detail in the AUTH_INVALID_ID_TOKEN_HINT error's server-side
// log line only (the client envelope stays generic).
const char *jwtVerificationName(fulla::oauth2::JwkManager::JwtVerificationResult result)
{
    using R = fulla::oauth2::JwkManager::JwtVerificationResult;
    switch (result)
    {
        case R::Ok:
            return "ok";
        case R::NotInitialized:
            return "jwk-not-initialized";
        case R::Malformed:
            return "malformed-jwt";
        case R::BadAlg:
            return "unsupported-alg";
        case R::KidMismatch:
            return "kid-mismatch";
        case R::BadSignature:
            return "bad-signature";
        case R::IssuerMismatch:
            return "issuer-mismatch";
        case R::Expired:
            return "expired";
        case R::NotYetValid:
            return "not-yet-valid";
        case R::MissingSubject:
            return "missing-sub";
        case R::AudienceMismatch:
            return "audience-mismatch";
    }
    return "unknown";
}

}  // namespace

using namespace ::drogon::orm;
using namespace ::drogon_model::fulla_db;

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
            successExample["version"] = FULLA_VERSION_TEXT;

            ::fulla::drogon::observability::openapi::EndpointInfo healthEndpoint;
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
            successExample["code"] = "xyz123";
            successExample["location"] = "http://127.0.0.1:5173/callback?code=xyz123&state=abc";

            Json::Value errorExample;
            errorExample["error"] = "invalid_client";

            ::fulla::drogon::observability::openapi::EndpointInfo loginEndpoint;
            loginEndpoint.path = "/oauth2/login";
            loginEndpoint.method = "POST";
            loginEndpoint.summary = "Authenticate user";
            loginEndpoint.description =
              "Authenticates user credentials and generates an authorization code. "
              "Usually called by the frontend login page during the authorization code flow.";
            loginEndpoint.tags = {"OAuth2", "Authentication"};

            ::fulla::drogon::observability::openapi::ParameterInfo usernameParam;
            usernameParam.name = "username";
            usernameParam.description = "User's account username (required)";
            usernameParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            usernameParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            usernameParam.required = true;

            ::fulla::drogon::observability::openapi::ParameterInfo passwordParam;
            passwordParam.name = "password";
            passwordParam.description = "User's password (required)";
            passwordParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            passwordParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            passwordParam.required = true;

            ::fulla::drogon::observability::openapi::ParameterInfo clientIdParam;
            clientIdParam.name = "client_id";
            clientIdParam.description = "Client identifier matches the requesting app (required)";
            clientIdParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            clientIdParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            clientIdParam.required = true;

            ::fulla::drogon::observability::openapi::ParameterInfo redirectUriParam;
            redirectUriParam.name = "redirect_uri";
            redirectUriParam.description = "Redirect URI matching the registered client (required)";
            redirectUriParam.type =
              ::fulla::drogon::observability::openapi::ParameterType::STRING;
            redirectUriParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            redirectUriParam.required = true;

            ::fulla::drogon::observability::openapi::ParameterInfo scopeParam;
            scopeParam.name = "scope";
            scopeParam.description = "Requested scope, space-separated (optional)";
            scopeParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            scopeParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            scopeParam.required = false;

            ::fulla::drogon::observability::openapi::ParameterInfo stateParam;
            stateParam.name = "state";
            stateParam.description = "Opaque value to maintain state (recommended)";
            stateParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            stateParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            stateParam.required = false;

            // PKCE (RFC 7636 / F-011): code_challenge is REQUIRED for PUBLIC
            // clients (vue-client, admin-console) when require_pkce_for_public
            // is enabled (default). The matching code_verifier goes on the
            // /oauth2/token exchange. Declared in the generated openapi.json
            // so client generators emit PKCE-aware clients.
            ::fulla::drogon::observability::openapi::ParameterInfo codeChallengeParam;
            codeChallengeParam.name = "code_challenge";
            codeChallengeParam.description =
              "PKCE code challenge (RFC 7636). Required for PUBLIC clients when "
              "auth.require_pkce_for_public is enabled (default true).";
            codeChallengeParam.type =
              ::fulla::drogon::observability::openapi::ParameterType::STRING;
            codeChallengeParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            codeChallengeParam.required = false;

            ::fulla::drogon::observability::openapi::ParameterInfo codeChallengeMethodParam;
            codeChallengeMethodParam.name = "code_challenge_method";
            codeChallengeMethodParam.description = "PKCE method: S256 (recommended) or plain.";
            codeChallengeMethodParam.type =
              ::fulla::drogon::observability::openapi::ParameterType::STRING;
            codeChallengeMethodParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            codeChallengeMethodParam.required = false;

            loginEndpoint.parameters = {usernameParam,
                                        passwordParam,
                                        clientIdParam,
                                        redirectUriParam,
                                        scopeParam,
                                        stateParam,
                                        codeChallengeParam,
                                        codeChallengeMethodParam};
            loginEndpoint.responses =
              {{200, "Authentication successful (JSON with redirect_uri); 200 with mfa_required=true when the account has MFA enabled, or password_change_required=true while the account is flagged must_change_password (#145)"},
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

            ::fulla::drogon::observability::openapi::EndpointInfo registerEndpoint;
            registerEndpoint.path = "/api/register";
            registerEndpoint.method = "POST";
            registerEndpoint.summary = "Register new user";
            registerEndpoint.description = "Registers a new user account into the system.";
            registerEndpoint.tags = {"User", "Registration"};

            ::fulla::drogon::observability::openapi::ParameterInfo usernameParam;
            usernameParam.name = "username";
            usernameParam.description = "Desired username (required)";
            usernameParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            usernameParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            usernameParam.required = true;

            ::fulla::drogon::observability::openapi::ParameterInfo passwordParam;
            passwordParam.name = "password";
            passwordParam.description = "Strong password (required)";
            passwordParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            passwordParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            passwordParam.required = true;

            ::fulla::drogon::observability::openapi::ParameterInfo emailParam;
            emailParam.name = "email";
            emailParam.description = "Email address (optional)";
            emailParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            emailParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
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
            ::fulla::drogon::observability::openapi::EndpointInfo consentEndpoint;
            consentEndpoint.path = "/oauth2/consent";
            consentEndpoint.method = "POST";
            consentEndpoint.summary = "Submit user consent";
            consentEndpoint.description =
              "Submit user consent for requested scopes. Redirects back to client.";
            consentEndpoint.tags = {"OAuth2", "Consent"};

            ::fulla::drogon::observability::openapi::ParameterInfo clientIdParam;
            clientIdParam.name = "client_id";
            clientIdParam.description = "Client identifier (required)";
            clientIdParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            clientIdParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            clientIdParam.required = true;

            ::fulla::drogon::observability::openapi::ParameterInfo userIdParam;
            userIdParam.name = "user_id";
            userIdParam.description = "User identifier (required)";
            userIdParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            userIdParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            userIdParam.required = true;

            ::fulla::drogon::observability::openapi::ParameterInfo scopeParam;
            scopeParam.name = "scope";
            scopeParam.description = "Requested scope to consent (required)";
            scopeParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            scopeParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            scopeParam.required = true;

            ::fulla::drogon::observability::openapi::ParameterInfo redirectUriParam;
            redirectUriParam.name = "redirect_uri";
            redirectUriParam.description = "Redirect URI (required)";
            redirectUriParam.type =
              ::fulla::drogon::observability::openapi::ParameterType::STRING;
            redirectUriParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            redirectUriParam.required = true;

            ::fulla::drogon::observability::openapi::ParameterInfo stateParam;
            stateParam.name = "state";
            stateParam.description = "Opaque value to maintain state";
            stateParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            stateParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            stateParam.required = false;

            ::fulla::drogon::observability::openapi::ParameterInfo actionParam;
            actionParam.name = "action";
            actionParam.description = "Action to perform: 'approve' or 'deny' (required)";
            actionParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            actionParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            actionParam.required = true;
            actionParam.enumValues = "approve,deny";

            ::fulla::drogon::observability::openapi::ParameterInfo consentCsrfParam;
            consentCsrfParam.name = "consent_csrf";
            consentCsrfParam.description =
              "Server-minted one-shot CSRF nonce from the authorize->consent redirect (required)";
            consentCsrfParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            consentCsrfParam.location =
              ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            consentCsrfParam.required = true;

            consentEndpoint.parameters =
              {clientIdParam, userIdParam, scopeParam, redirectUriParam, stateParam, actionParam,
               consentCsrfParam};
            consentEndpoint.responses = {
              {302, "Redirect to client with authorization code or error"},
              {400, "Missing/expired/mismatched consent_csrf nonce, or deny redirect_uri not registered"},
              {401, "No authenticated session (AUTH_SESSION_REQUIRED), or MFA-pending session (AUTH_MFA_REQUIRED, #144)"},
              {403, "user_id does not match the session (AUTHZ_ACCESS_DENIED), or must_change_password flagged (AUTH_PASSWORD_CHANGE_REQUIRED, #145)"}
            };
            consentEndpoint.requiresAuth = false;
            OpenApiGenerator::addEndpoint(consentEndpoint);
        }

        // Forced password-change endpoint (#145: session-authenticated, only
        // usable while the session carries the must_change_password marker)
        {
            ::fulla::drogon::observability::openapi::EndpointInfo pwdChangeEndpoint;
            pwdChangeEndpoint.path = "/oauth2/password/change";
            pwdChangeEndpoint.method = "POST";
            pwdChangeEndpoint.summary = "Change password (forced first-login flow)";
            pwdChangeEndpoint.description =
              "Changes the password of the session user while the account is "
              "flagged must_change_password (bootstrap admin, admin-created "
              "users). Requires old_password; clears the flag, revokes all "
              "tokens, and keeps the current session.";
            pwdChangeEndpoint.tags = {"OAuth2", "Session"};

            ::fulla::drogon::observability::openapi::ParameterInfo oldPwdParam;
            oldPwdParam.name = "old_password";
            oldPwdParam.description = "Current password (required)";
            oldPwdParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            oldPwdParam.location = ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            oldPwdParam.required = true;

            ::fulla::drogon::observability::openapi::ParameterInfo newPwdParam;
            newPwdParam.name = "new_password";
            newPwdParam.description = "New password (required, min length from auth.min_password_length)";
            newPwdParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
            newPwdParam.location = ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
            newPwdParam.required = true;

            pwdChangeEndpoint.parameters = {oldPwdParam, newPwdParam};
            pwdChangeEndpoint.responses = {
              {200, "Password changed; must_change_password flag cleared"},
              {400, "Missing fields or new password below the configured minimum"},
              {401, "No session / session without the must_change_password marker (AUTH_SESSION_REQUIRED), or wrong old_password (AUTH_INVALID_CREDENTIALS)"}
            };
            pwdChangeEndpoint.requiresAuth = false;
            OpenApiGenerator::addEndpoint(pwdChangeEndpoint);
        }

        // Logout endpoint (Bearer-protected programmatic logout)
        {
            ::fulla::drogon::observability::openapi::EndpointInfo logoutEndpoint;
            logoutEndpoint.path = "/oauth2/logout";
            logoutEndpoint.method = "POST";
            logoutEndpoint.summary = "Logout";
            logoutEndpoint.description =
              "Terminates the server-side session behind the presented Bearer "
              "access token (also fires OIDC back-channel logout notifications "
              "when configured).";
            logoutEndpoint.tags = {"OAuth2", "Session"};
            logoutEndpoint.responses = {{200, "Logged out"}};
            logoutEndpoint.requiresAuth = true;
            OpenApiGenerator::addEndpoint(logoutEndpoint);
        }

        // OIDC RP-Initiated Logout (GET link-based + POST form-based)
        {
            auto endSessionParam = [](const char *name, const char *desc) {
                ::fulla::drogon::observability::openapi::ParameterInfo p;
                p.name = name;
                p.description = desc;
                p.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
                p.location = ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
                p.required = false;
                return p;
            };

            ::fulla::drogon::observability::openapi::EndpointInfo endSessionGet;
            endSessionGet.path = "/oauth2/end_session";
            endSessionGet.method = "GET";
            endSessionGet.summary = "RP-Initiated Logout";
            endSessionGet.description =
              "OIDC RP-Initiated Logout 1.0 §2 (link-based variant). Terminates "
              "the user's server-side session and optionally redirects to a "
              "registered post_logout_redirect_uri.";
            endSessionGet.tags = {"OAuth2", "OIDC"};
            endSessionGet.parameters = {
              endSessionParam("id_token_hint", "Previously issued id_token hinting at the client/session to terminate."),
              endSessionParam("post_logout_redirect_uri", "URI to redirect to after logout; must be registered for the id_token_hint client."),
              endSessionParam("state", "Opaque value echoed back to the post_logout_redirect_uri."),
            };
            endSessionGet.responses = {
              {200, "Logged out (no post_logout_redirect_uri supplied)"},
              {302, "Redirect to the validated post_logout_redirect_uri (with state)"},
              {400, "post_logout_redirect_uri not registered / no id_token_hint"},
            };
            endSessionGet.requiresAuth = false;
            OpenApiGenerator::addEndpoint(endSessionGet);

            ::fulla::drogon::observability::openapi::EndpointInfo endSessionPost;
            endSessionPost.path = "/oauth2/end_session";
            endSessionPost.method = "POST";
            endSessionPost.summary = "RP-Initiated Logout (POST)";
            endSessionPost.description =
              "OIDC RP-Initiated Logout (POST form-based variant; see GET for semantics).";
            endSessionPost.tags = {"OAuth2", "OIDC"};
            endSessionPost.parameters = endSessionGet.parameters;
            endSessionPost.responses = endSessionGet.responses;
            endSessionPost.requiresAuth = false;
            OpenApiGenerator::addEndpoint(endSessionPost);
        }

        // Health sub-probes (liveness / readiness). Registered here, next to
        // the existing /health entry, because HealthController itself has no
        // doc-registration idiom; the static-ctor self-registers in both the
        // server and the test binary.
        {
            ::fulla::drogon::observability::openapi::EndpointInfo liveEndpoint;
            liveEndpoint.path = "/health/live";
            liveEndpoint.method = "GET";
            liveEndpoint.summary = "Liveness probe";
            liveEndpoint.description = "Process-is-alive check (always 200 when the server runs).";
            liveEndpoint.tags = {"System"};
            liveEndpoint.responses = {{200, "Service is alive"}};
            liveEndpoint.requiresAuth = false;
            OpenApiGenerator::addEndpoint(liveEndpoint);

            ::fulla::drogon::observability::openapi::EndpointInfo readyEndpoint;
            readyEndpoint.path = "/health/ready";
            readyEndpoint.method = "GET";
            readyEndpoint.summary = "Readiness probe";
            readyEndpoint.description =
              "Dependency readiness check (database / redis); 503 when a "
              "required dependency is down.";
            readyEndpoint.tags = {"System"};
            readyEndpoint.responses = {{200, "Service is ready"}, {503, "A dependency is down"}};
            readyEndpoint.requiresAuth = false;
            OpenApiGenerator::addEndpoint(readyEndpoint);
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
    // P0 #1 (评审问题点 1): OIDC nonce must survive the login round-trip the
    // same way the PKCE pair does, or it is silently dropped on this path.
    std::string nonce = params["nonce"];

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
    data["nonce"] = nonce;
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
    auto errors = ::fulla::drogon::validation::RuleSet::login(req);

    // Return validation errors if any
    if (
      ::fulla::drogon::validation::HttpResponder::respondIfErrors(errors, std::move(callback))
    )
    {
        if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
            m->incrementCounter(
              "oauth2_login_failures_total",
              fulla::common::ports::MetricLabels{{"reason", "validation_failed"}}
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

    // Task 24 slice 4 (fulla-sdk-refactor): validateUser's continuation
    // is identical regardless of which AuthService implementation ran it.
    // Phase 1.5a (Task 39, direction Y) retracted
    // fulla::identity::AuthResult.internalId to int32_t (aligned with the
    // legacy drogon::services::AuthResult and the int4 DB column), so the
    // previously-widened int64_t bridge here is gone: both branches below
    // (new identity::AuthService if injected, else the legacy
    // drogon::services::AuthService fallback) funnel into the exact same
    // int32 logic -- no duplicated CHECK 1/2/3 chain to keep in sync.
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
                         int32_t internalId,
                         std::string publicSub,
                         bool emailVerified,
                         bool mfaEnabled,
                         bool mustChangePassword
                       ) mutable {
        if (success)
        {
            req->session()->insert("userId", std::to_string(internalId));
            // #55: also remember the PUBLIC subject (the id_token sub) on the
            // session. endSession needs it to attribute the logout to a user
            // for backchannel notification -- the internal id above does not
            // match oauth2_access_tokens.user_id (which stores public_sub).
            req->session()->insert("sub", publicSub);
            // F-021/F-022 (OIDC Core §2/§3.1.3.7): record the auth_time and
            // amr on the session so the authorization-code issuance paths
            // (silent re-auth in AuthorizationEndpointController, the
            // /oauth2/consent handler here) can thread them onto the code,
            // and so the id_token eventually carries auth_time/acr/amr.
            // Password-only login = amr "pwd"; the MFA verify handler
            // (MfaController::verifyLogin) appends "mfa" and refreshes
            // auth_time when the second factor completes.
            auto nowSecs = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch()
            )
                             .count();
            req->session()->insert("auth_time", static_cast<int64_t>(nowSecs));
            req->session()->insert("amr", std::string("pwd"));
            // #144: a fresh password login is the newest authentication
            // state -- drop any stale MFA-pending marker (e.g. MFA was
            // administratively disabled while a session sat at the challenge
            // screen; without this the authorize gate would redirect that
            // session to /login forever).
            req->session()->erase("mfa_pending");
            // #145: symmetric stale-marker cleanup for the password-change
            // flag (Session::insert never overwrites, so a stale true must be
            // erased explicitly; it is re-inserted below only when still set
            // in the users row).
            req->session()->erase("must_change_password");

            // Audit: login success
            ::fulla::drogon::adapters::DrogonAuditSink::logFromRequest(
              ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
              "login_success",
              "success",
              req,
              publicSub,
              "user",
              publicSub
            );

            // === #145: forced first-login password change ===
            // Checked BEFORE evaluateLoginPolicy: changing the password needs
            // only the first factor; after a successful change the user
            // re-logs-in and MFA (if enabled) proceeds from a clean state.
            // While flagged, no authorization codes are issued for this
            // account (authorize redirects to the change-password page,
            // consent answers 403 AUTH_PASSWORD_CHANGE_REQUIRED, and
            // MfaController::verifyLogin refuses to mint a code) -- the
            // change itself happens via POST /oauth2/password/change on this
            // session.
            if (mustChangePassword)
            {
                req->session()->insert("must_change_password", true);
                if (req->getParameter("json") == "true")
                {
                    Json::Value pwdResp;
                    pwdResp["password_change_required"] = true;
                    pwdResp["message"] =
                      "Password change required. Change the password via "
                      "POST /oauth2/password/change before signing in";
                    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(pwdResp);
                    resp->setStatusCode(::drogon::k200OK);
                    callback(resp);
                    return;
                }
                auto customCfg = ::drogon::app().getCustomConfig();
                std::string frontendUrl = "http://localhost:5173";
                if (
                  customCfg.isMember("frontend") && customCfg["frontend"].isMember("url")
                )
                {
                    frontendUrl = customCfg["frontend"]["url"].asString();
                }
                if (!frontendUrl.empty() && frontendUrl.back() == '/')
                    frontendUrl.pop_back();
                auto resp = ::drogon::HttpResponse::newRedirectionResponse(
                  frontendUrl + "/login?must_change_password=1"
                );
                callback(resp);
                return;
            }

            // === CHECK 1/2: email verification + MFA enforcement ===
            // Task 24 slice 4: delegates to
            // fulla::identity::evaluateLoginPolicy() (design.md §5.1/§6),
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

            fulla::identity::AuthResult policyInput;
            policyInput.internalId = internalId;
            policyInput.publicSub = publicSub;
            policyInput.emailVerified = emailVerified;
            policyInput.mfaEnabled = mfaEnabled;
            auto decision =
              fulla::identity::evaluateLoginPolicy(policyInput, requireEmailVerification);

            if (decision == fulla::identity::LoginDecision::DenyEmailNotVerified)
            {
                respondError(
                  req, std::move(callback), "AUTHZ_ACCESS_DENIED", "login: email not verified"
                );
                return;
            }

            if (decision == fulla::identity::LoginDecision::RequireMfa)
            {
                auto sharedCb =
                  std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
                    std::move(callback)
                  );
                auto db = ::drogon::app().getDbClient();
                // users.id is a Postgres `integer` (32-bit) column; internalId
                // is int32_t all the way through (Task 39 direction Y), so it
                // binds as int4 with no narrowing step needed.
                // Task B5: replaced raw SQL with Mapper<Users>
                Criteria mfaCrit(Users::Cols::_id, CompareOperator::EQ, internalId);
                Mapper<Users>(db).findOne(
                  mfaCrit,
                  [req, internalId, sharedCb, db, clientId, redirectUri, codeChallenge, codeChallengeMethod](const Users &user) {
                      Users mfaUpdated = user;
                      mfaUpdated.setMfaPendingClientId(clientId);
                      mfaUpdated.setMfaPendingRedirectUri(redirectUri);
                      Mapper<Users>(db).update(
                        mfaUpdated,
                        [req, internalId, sharedCb, codeChallenge, codeChallengeMethod](const size_t) {
                            // C4 (RFC 7636): persist the first-factor PKCE
                            // challenge on the session so MfaController::
                            // verifyLogin can thread it onto the authorization
                            // code it generates at MFA completion. Without this,
                            // the MFA path issued tokens with empty PKCE params
                            // (no protection for PUBLIC clients — F-011 gap).
                            // Session-based (not a DB column) because the
                            // challenge is short-lived and the session already
                            // carries userId/auth_time/amr across the MFA pause.
                            if (req->session())
                            {
                                req->session()->insert("mfa_code_challenge", codeChallenge);
                                req->session()->insert("mfa_code_challenge_method", codeChallengeMethod);
                                // #144: mark the session as paused at the MFA
                                // challenge. Consent/authorize gates treat a
                                // pending session as NOT fully authenticated
                                // (amr is still "pwd" here); the marker is
                                // erased by MfaController::verifyLogin on
                                // successful second-factor completion or by
                                // the next password login.
                                req->session()->insert("mfa_pending", true);
                            }
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

            // === CHECK 3: PKCE enforcement ===
            // F-011 (RFC 9700 §2.1.1): PKCE is MANDATORY for all OAuth 2.0
            // authorization_code clients, so the code default is true when the
            // config key is absent; auth.require_pkce_for_public can still
            // opt a deployment out explicitly.
            bool requirePkce = true;
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
                // F-007 (RFC 6749 §4.1.2.1): this error belongs to the
                // authorization request, so it is redirected back to the
                // client -- but only after redirect_uri is verified, since
                // /oauth2/login does not re-validate it earlier in the flow
                // (avoids an open-redirect vector).
                auto pkcePlugin = resolvePlugin();
                if (pkcePlugin && !clientId.empty() && !redirectUri.empty())
                {
                    pkcePlugin->validateRedirectUri(
                      clientId,
                      redirectUri,
                      [req, redirectUri, state, callback = std::move(callback)](
                        bool validUri
                      ) mutable {
                          if (validUri)
                          {
                              sendOAuthErrorRedirect(
                                callback,
                                redirectUri,
                                "invalid_request",
                                "PKCE (code_challenge) is required for public clients",
                                state
                              );
                              return;
                          }
                          respondError(
                            req,
                            std::move(callback),
                            "VALIDATION_MISSING_REQUIRED_FIELD",
                            "login: PKCE (code_challenge) is required for public clients"
                          );
                      }
                    );
                    return;
                }
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

            // F-022 (OIDC Core §3.1.3.7): read the auth_time/amr we just
            // recorded on the session (password-only -> "pwd") so the
            // authorization code carries them to the id_token issuance path.
            int64_t sessAuthTime = 0;
            std::string sessAmr;
            if (req->session())
            {
                if (req->session()->find("auth_time"))
                    sessAuthTime = req->session()->get<int64_t>("auth_time");
                if (req->session()->find("amr"))
                    sessAmr = req->session()->get<std::string>("amr");
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

                  // F-020 (RFC 6749 §4.1.2/§4.1.3): urlEncode code + state.
                  std::string location =
                    redirectUri + "?code=" + ::drogon::utils::urlEncode(code);
                  if (!state.empty())
                      location += "&state=" + ::drogon::utils::urlEncode(state);
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
              },
              sessAuthTime,
              sessAmr
            );
        }
        else
        {
            // Fail (Bad Password or User Not Found)
            if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                m->incrementCounter(
                  "oauth2_login_failures_total",
                  fulla::common::ports::MetricLabels{{"reason", "bad_credentials"}}
                );

            // Audit: login failure
            ::fulla::drogon::adapters::DrogonAuditSink::logFromRequest(
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

    // Task 24 slice 4: prefer the injected fulla::identity::AuthService
    // (constructed once at startup by
    // bootstrap::wireIdentityServices()/OAuth2Server/bootstrap/
    // IdentityAssembly.cc, backed by PostgresIdentityRepository +
    // OpenSslCryptoProvider + SystemClock -- see that file for the
    // construction site) when wired; otherwise fall back to the
    // pre-Task-24 fulla::drogon::services::AuthService (static,
    // Mapper<Users>-backed) so this controller keeps working unchanged in
    // any binary that has not called setIdentityAuthService() yet (e.g.
    // tests/e2e-backend's direct-construction tests, until they are
    // updated). Both AuthResult shapes carry an int32 internalId (Task 39
    // direction Y), matching onValidated's signature above.
    if (identityAuthService_)
    {
        identityAuthService_->validateUser(
          username,
          password,
          [onValidated = std::move(onValidated)](
            std::optional<fulla::identity::AuthResult> result
          ) mutable {
              if (!result)
              {
                  onValidated(false, 0, "", false, false, false);
                  return;
              }
              onValidated(
                true,
                result->internalId,
                result->publicSub,
                result->emailVerified,
                result->mfaEnabled,
                result->mustChangePassword
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
                  onValidated(false, 0, "", false, false, false);
                  return;
              }
              onValidated(
                true,
                result->internalId,
                result->publicSub,
                result->emailVerified,
                result->mfaEnabled,
                result->mustChangePassword
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
    std::string consentCsrf = params["consent_csrf"];

    // F1 (consent auth gates, fail-closed before ANY plugin call):
    // Gate 1 — the request must belong to an authenticated session. A
    // session without `sub` was never logged in; consent cannot be granted
    // anonymously (OIDC Core §3.1.2.1: the authorization endpoint MUST NOT
    // fulfill a request without authenticating the end-user).
    std::string sessSub;
    std::string sessUserId;
    if (req->session())
    {
        if (req->session()->find("sub"))
            sessSub = req->session()->get<std::string>("sub");
        if (req->session()->find("userId"))
            sessUserId = req->session()->get<std::string>("userId");
    }
    if (sessSub.empty() || sessUserId.empty())
    {
        respondError(
          req, std::move(callback), "AUTH_SESSION_REQUIRED",
          "consent: an authenticated session is required"
        );
        return;
    }

    // Gate 2 — the consented user must be the session user. The authorize
    // flow hands `user_id` = session["userId"] (internal id as string);
    // a mismatch means someone is submitting consent for another user.
    if (userId != sessUserId)
    {
        respondError(
          req, std::move(callback), "AUTHZ_ACCESS_DENIED",
          "consent: user_id does not match the authenticated session"
        );
        return;
    }

    // Gate 2a (#144) — a session paused at the MFA challenge passed Gate 1
    // (login writes userId/sub before the MFA decision), but amr is still
    // "pwd": consent granted on it would mint a code for an MFA-required
    // account without the second factor ever completing. Refuse until
    // MfaController::verifyLogin clears the marker.
    if (req->session()->find("mfa_pending") && req->session()->get<bool>("mfa_pending"))
    {
        respondError(
          req, std::move(callback), "AUTH_MFA_REQUIRED",
          "consent: complete MFA verification before granting consent"
        );
        return;
    }

    // Gate 2b (#145) — accounts flagged must_change_password must change the
    // password (POST /oauth2/password/change) before any authorization code
    // is issued; consent submissions are refused meanwhile.
    if (
      req->session()->find("must_change_password") &&
      req->session()->get<bool>("must_change_password")
    )
    {
        respondError(
          req, std::move(callback), "AUTH_PASSWORD_CHANGE_REQUIRED",
          "consent: change the account password before granting consent"
        );
        return;
    }

    // Gate 3 — CSRF nonce (server-minted at the authorize->consent redirect;
    // deliberately NOT the client-chosen `state`, which is attacker-known).
    // One-shot per nonce: consumed here so replays fail; TTL 10 minutes.
    // #144: the nonce lives in a bounded multi-slot list
    // (ConsentCsrfSlots) so concurrent authorize flows on one session no
    // longer overwrite each other; the helper holds the process-wide mutex
    // across its full read-modify-write (cross-instance deployments share
    // session storage only with sticky routing — same limitation as the
    // rest of the in-memory session surface).
    {
        const int64_t now = static_cast<int64_t>(
          ::trantor::Date::now().secondsSinceEpoch()
        );
        if (!::fulla::drogon::utils::ConsentCsrfSlots::consume(req->session(), consentCsrf, now))
        {
            respondError(
              req, std::move(callback), "VALIDATION_INVALID_INPUT",
              "consent: missing, expired, or mismatched consent_csrf"
            );
            return;
        }
    }

    // F-022 (OIDC Core §3.1.3.7): read auth_time/amr from the session so the
    // consent-issued code carries them to the id_token. Consent always
    // follows a login that populated these on the session; if absent (e.g.
    // a direct POST without a session), pass 0/"" and the id_token omits
    // auth_time/amr (acceptable per OIDC -- they are conditionally required).
    int64_t sessAuthTime = 0;
    std::string sessAmr;
    if (req->session())
    {
        if (req->session()->find("auth_time"))
            sessAuthTime = req->session()->get<int64_t>("auth_time");
        if (req->session()->find("amr"))
            sessAmr = req->session()->get<std::string>("amr");
    }

    if (action == "deny")
    {
        // Gate 4 — the deny redirect must go to a redirect_uri registered
        // for the client (RFC 6749 §3.1.2.2); an unvalidated 302 here is an
        // open redirect. (The approve branch derives its redirect target
        // from the authorize request itself and is gated by Gate 2; adding
        // the same validation there is tracked as follow-up.) validateRedirectUri
        // alone is used on purpose: it looks the client up itself, and
        // validateClient(clientId, "") would false-reject CONFIDENTIAL
        // clients (empty secret is rejected by the repository).
        auto plugin = resolvePlugin();
        if (!plugin)
        {
            respondError(
              req, std::move(callback), "INTERNAL_ERROR",
              "consent: OAuth2 Plugin not loaded"
            );
            return;
        }
        auto sharedCb =
          std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
            std::move(callback)
          );
        plugin->validateRedirectUri(
          clientId,
          redirectUri,
          [req, clientId, redirectUri, state, sharedCb](bool validUri) mutable {
              if (!validUri)
              {
                  respondError(
                    req, *sharedCb, "VALIDATION_REDIRECT_URI_NOT_REGISTERED",
                    "consent: deny redirect_uri is not registered for the client"
                  );
                  return;
              }
              std::string location =
                redirectUri +
                "?error=access_denied&error_description=User+denied+consent";
              if (!state.empty())
                  location += "&state=" + ::drogon::utils::urlEncode(state);
              (*sharedCb)(::drogon::HttpResponse::newRedirectionResponse(location));
          }
        );
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
       sessAuthTime,
       sessAmr,
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
                 sessAuthTime,
                 sessAmr,
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
                              LOG_ERROR << "consent: failed to generate authorization code: "
                                        << error;
                              // F-007: server_error redirects back to the
                              // client per RFC 6749 §4.1.2.1.
                              sendOAuthErrorRedirect(
                                callback,
                                redirectUri,
                                "server_error",
                                "Failed to generate authorization code",
                                state
                              );
                              return;
                          }

                          // F-020 (RFC 6749 §4.1.2/§4.1.3): urlEncode code + state.
                          std::string location =
                            redirectUri + "?code=" + ::drogon::utils::urlEncode(code);
                          if (!state.empty())
                              location += "&state=" + ::drogon::utils::urlEncode(state);
                          auto resp = ::drogon::HttpResponse::newRedirectionResponse(location);
                          if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                              m->incrementCounter(
                                "oauth2_requests_total",
                                fulla::common::ports::MetricLabels{{"endpoint", "authorize"}},
                                static_cast<double>(302)
                              );
                          callback(resp);
                      },
                      sessAuthTime,
                      sessAmr
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
                        LOG_ERROR << "consent: failed to generate authorization code: " << error;
                        // F-007: server_error redirects back to the client
                        // per RFC 6749 §4.1.2.1.
                        sendOAuthErrorRedirect(
                          callback,
                          redirectUri,
                          "server_error",
                          "Failed to generate authorization code",
                          state
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
                          fulla::common::ports::MetricLabels{{"endpoint", "authorize"}},
                          static_cast<double>(302)
                        );
                    callback(resp);
                },
                sessAuthTime,
                sessAmr
              );
          }
      }
    );
}

// #145: forced first-login password change. Unlike PUT /api/me/password
// (Bearer-token protected), this endpoint authenticates via the browser
// session -- a must_change_password user cannot obtain tokens (all code
// issuance is gated), so the session that just passed the first factor is
// the only credential available. Hardened by:
//   * only usable while the session carries the must_change_password marker
//     (set at login from the users row; cleared here on success),
//   * old_password is always verified against the stored hash (same
//     knowledge factor as the login itself),
//   * the password policy (auth.min_password_length) applies,
//   * on success all access/refresh tokens are revoked (same documented
//     batch exemption as PUT /api/me/password) and the audit trail records
//     the change.
void SessionController::changePasswordForced(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    // Session must exist, identify a user, and carry the marker.
    if (
      !req->session() || !req->session()->find("userId") ||
      req->session()->get<std::string>("userId").empty() ||
      !req->session()->find("must_change_password") ||
      !req->session()->get<bool>("must_change_password")
    )
    {
        respondError(
          req,
          *sharedCb,
          "AUTH_SESSION_REQUIRED",
          "password/change: only available to a session flagged must_change_password"
        );
        return;
    }
    const std::string sessUserId = req->session()->get<std::string>("userId");
    const std::string sessSub = req->session()->find("sub")
                                  ? req->session()->get<std::string>("sub")
                                  : sessUserId;

    std::string oldPassword;
    std::string newPassword;
    if (req->getJsonObject())
    {
        oldPassword = req->getJsonObject()->get("old_password", "").asString();
        newPassword = req->getJsonObject()->get("new_password", "").asString();
    }
    else
    {
        oldPassword = req->getParameter("old_password");
        newPassword = req->getParameter("new_password");
    }

    if (oldPassword.empty() || newPassword.empty())
    {
        respondError(
          req,
          *sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "password/change: old_password and new_password are required"
        );
        return;
    }

    if (newPassword.length() < fulla::drogon::validation::RuleSet::passwordMinLength())
    {
        respondError(
          req,
          *sharedCb,
          "VALIDATION_PASSWORD_TOO_SHORT",
          "password/change: new password must be at least " +
            std::to_string(fulla::drogon::validation::RuleSet::passwordMinLength()) + " characters"
        );
        return;
    }

    auto db = ::drogon::app().getDbClient();
    // users.id is int4; the session stores the internal id as string.
    int internalId = 0;
    try
    {
        internalId = std::stoi(sessUserId);
    }
    catch (...)
    {
        respondError(
          req, *sharedCb, "AUTH_SESSION_REQUIRED", "password/change: invalid session user id"
        );
        return;
    }

    try
    {
        Criteria pwCrit(Users::Cols::_id, CompareOperator::EQ, internalId);
        Criteria deletedCrit(Users::Cols::_deleted_at, CompareOperator::IsNull);
        Mapper<Users>(db).findBy(
          pwCrit && deletedCrit,
          [sharedCb, req, db, oldPassword, newPassword, internalId, sessSub](
            const std::vector<Users> &users
          ) {
              if (users.empty())
              {
                  respondError(
                    req, *sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "password/change: user not found"
                  );
                  return;
              }
              const Users &user = users[0];
              if (!::fulla::common::utils::PasswordHasher::verify(
                    oldPassword, user.getValueOfPasswordHash(), user.getValueOfSalt()
                  ))
              {
                  ::fulla::drogon::adapters::DrogonAuditSink::logFromRequest(
                    ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                    "password_change_failed",
                    "failure",
                    req,
                    sessSub,
                    "user",
                    sessSub
                  );
                  respondError(
                    req,
                    *sharedCb,
                    "AUTH_INVALID_CREDENTIALS",
                    "password/change: current password is incorrect"
                  );
                  return;
              }

              std::string newHash;
              try
              {
                  newHash = ::fulla::common::utils::PasswordHasher::hash(newPassword);
              }
              catch (const std::exception &e)
              {
                  respondError(
                    req, *sharedCb, "INTERNAL_ERROR", std::string("Password hashing failed: ") + e.what()
                  );
                  return;
              }

              Users updatedUser = user;
              updatedUser.setPasswordHash(newHash);
              updatedUser.setSalt("");
              updatedUser.setMustChangePassword(false);
              try
              {
                  Mapper<Users>(db).update(
                    updatedUser,
                    [sharedCb, req, db, sessSub](const size_t) {
                        // Exemption (db-operations.md §3): security-critical
                        // batch revoke on password change (same as PUT
                        // /api/me/password). The current browser session is
                        // kept so the user can continue signing in.
                        db->execSqlAsync(
                          "UPDATE oauth2_access_tokens SET revoked = true WHERE user_id = $1",
                          [sharedCb, req, db, sessSub](const ::drogon::orm::Result &) {
                              db->execSqlAsync(
                                "UPDATE oauth2_refresh_tokens SET revoked = true WHERE user_id = $1",
                                [sharedCb, req, sessSub](const ::drogon::orm::Result &) {
                                    if (req->session())
                                        req->session()->erase("must_change_password");
                                    ::fulla::drogon::adapters::DrogonAuditSink::logFromRequest(
                                      ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                                      "password_changed",
                                      "success",
                                      req,
                                      sessSub,
                                      "user",
                                      sessSub
                                    );
                                    Json::Value json;
                                    json["message"] =
                                      "Password changed successfully. Sign in again to continue.";
                                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                                },
                                [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                                    respondError(
                                      req, *sharedCb, "DB_QUERY_ERROR",
                                      std::string("Failed to revoke refresh tokens: ") + e.base().what()
                                    );
                                },
                                sessSub
                              );
                          },
                          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                              respondError(
                                req, *sharedCb, "DB_QUERY_ERROR",
                                std::string("Failed to revoke access tokens: ") + e.base().what()
                              );
                          },
                          sessSub
                        );
                    },
                    [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                        respondError(
                          req, *sharedCb, "DB_QUERY_ERROR",
                          std::string("Failed to update password: ") + e.base().what()
                        );
                    }
                  );
              }
              catch (const std::exception &e)
              {
                  respondError(
                    req, *sharedCb, "DB_QUERY_ERROR",
                    std::string("Failed to update password: ") + e.what()
                  );
              }
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req, *sharedCb, "DB_QUERY_ERROR",
                std::string("Failed to load user: ") + e.base().what()
              );
          }
        );
    }
    catch (const std::exception &e)
    {
        respondError(
          req, *sharedCb, "DB_QUERY_ERROR", std::string("Failed to load user: ") + e.what()
        );
    }
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

    // F-028 (OIDC RP-Initiated Logout): terminate the server-side session in
    // addition to revoking the access token. logout() is called with a bearer
    // token (API-style), so the session may be absent (e.g. M2M callers); the
    // guard keeps that path a no-op rather than a crash.
    if (req->session())
        req->session()->clear();

    // Revoke the access token, then notify relying parties via the injected
    // SessionManager (whose notifier POSTs a signed logout_token to each RP
    // with an active session + a registered backchannel_logout_uri, see
    // bootstrap::wireIdentityServices() -> BackchannelLogoutNotifier).
    // sessionManager_ is null only in memory-storage / no-DB configurations,
    // where there are no oauth2_clients rows to notify -- so the else branch
    // simply responds.
    auto *sessionManager = sessionManager_;
    plugin->revokeAccessToken(
      token, clientId, [userId, sessionManager, callback = std::move(callback)]() mutable {
          LOG_INFO << "Logout: Token revoked for user " << userId;

          auto respond = [callback = std::move(callback)]() mutable {
              // Respond immediately: notify() is fire-and-forget from the
              // caller's perspective (the notifier's POSTs to RPs do not
              // block this response).
              Json::Value json;
              json["message"] = "Logged out successfully";
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              resp->setStatusCode(::drogon::k200OK);
              callback(resp);
          };

          if (sessionManager)
              sessionManager->logout(userId, std::move(respond));
          else
              respond();
      }
    );
}

void SessionController::endSession(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    // F-027 (OIDC RP-Initiated Logout 1.0 §2): terminate the user's session
    // at the OP and (optionally) redirect to a registered post_logout_redirect_uri.
    // Accepts both GET (link-based) and POST (form-based) per §2.1.
    auto params = req->getParameters();
    std::string idTokenHint = params["id_token_hint"];
    std::string postLogoutRedirectUri = params["post_logout_redirect_uri"];
    std::string state = params["state"];

    // Validate post_logout_redirect_uri: if id_token_hint is present (and,
    // since #78, signature-verified -- see the gate below), use its aud claim
    // to find the client_id, then require the redirect URI to be one of that
    // client's registered redirect_uris. If id_token_hint is absent, only
    // accept the redirect URI when it is empty (the spec allows the server to
    // require pre-registration; this server does so for safety).
    auto plugin = resolvePlugin();

    // #78: an id_token_hint MUST pass end-to-end verification (RS256 signature
    // against our own key set, strict alg, kid, issuer, exp) before ANY of its
    // claims influence the logout decision. Previously the payload was decoded
    // without verification and its `sub` drove the backchannel fan-out, so
    // anyone who knew a user's public subject could force that user's logout
    // at every RP. Any failure is a hard 400 (AUTH_INVALID_ID_TOKEN_HINT) --
    // there is deliberately NO silent fallback to "treat as if no hint was
    // supplied". Every rejection returns BEFORE finish() is reachable, so no
    // session is cleared and no relying party is notified.
    Json::Value hintPayload;
    std::string hintSub;
    if (!idTokenHint.empty())
    {
        auto jwkManager = plugin ? plugin->getJwkManager() : nullptr;
        if (!plugin || !jwkManager)
        {
            respondError(
              req,
              callback,
              "INTERNAL_ERROR",
              "end_session: OAuth2Plugin/JwkManager not available for id_token_hint verification"
            );
            return;
        }
        const long long nowSecs = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch()
        )
                                    .count();
        // #87 L2: verifyAndDecode returns the already-verified payload, so
        // the former second base64+JSON pass (decodeJwtPayloadClaims) is gone.
        fulla::oauth2::JwkManager::JwtVerificationResult verification =
          fulla::oauth2::JwkManager::JwtVerificationResult::Malformed;
        auto verifiedPayload =
          jwkManager->verifyAndDecode(idTokenHint, plugin->getIssuer(), nowSecs, {}, &verification);
        if (!verifiedPayload.has_value())
        {
            respondError(
              req,
              callback,
              "AUTH_INVALID_ID_TOKEN_HINT",
              std::string("end_session: id_token_hint rejected: ") +
                jwtVerificationName(verification)
            );
            return;
        }
        hintPayload = std::move(*verifiedPayload);
        if (hintPayload.isMember("sub") && hintPayload["sub"].isString())
            hintSub = hintPayload["sub"].asString();
        // Subject consistency: with BOTH a browser session and a verified
        // hint, the hint must describe the signed-in user. A mismatch
        // (session of user A + hint of user B) is rejected; the caller has
        // to re-authenticate. An empty session subject (pre-login session)
        // does not participate in the check.
        if (req->session())
        {
            std::string sessionSub = req->session()->get<std::string>("sub");
            if (!sessionSub.empty() && !hintSub.empty() && sessionSub != hintSub)
            {
                respondError(
                  req,
                  callback,
                  "AUTH_INVALID_ID_TOKEN_HINT",
                  "end_session: id_token_hint subject does not match the current session"
                );
                return;
            }
        }
    }

    // #55: attribute the logout to a user for backchannel notification.
    // Preference order: the session's public subject (set by login(); the
    // internal-id "userId" session attr does NOT match
    // oauth2_access_tokens.user_id), then the VERIFIED id_token_hint's sub
    // claim (#78: unreachable for unverified hints -- they 400 above).
    // Bearer-style callers without either stay unattributed (no notify).
    //
    // #88: the aud claim may be a string OR an array of strings (RFC 7519
    // §4.1.3; OIDC allows multi-audience id_tokens). Collect every string
    // element as a client-identification candidate; the post-logout redirect
    // validation tries each against the client registry (fail-closed only
    // when none validates).
    std::vector<std::string> audCandidates;
    if (hintPayload.isMember("aud"))
    {
        if (hintPayload["aud"].isString())
        {
            if (!hintPayload["aud"].asString().empty())
                audCandidates.push_back(hintPayload["aud"].asString());
        }
        else if (hintPayload["aud"].isArray())
        {
            for (const auto &element : hintPayload["aud"])
            {
                if (element.isString() && !element.asString().empty())
                    audCandidates.push_back(element.asString());
            }
        }
        // A present-but-unusable aud structure (e.g. [42]) is not an
        // id_token this OP would issue: reject rather than degrade to the
        // no-hint path below.
        if (!idTokenHint.empty() && audCandidates.empty())
        {
            respondError(
              req,
              callback,
              "AUTH_INVALID_ID_TOKEN_HINT",
              "end_session: id_token_hint aud claim carries no usable client identifier"
            );
            return;
        }
    }
    std::string subject;
    if (req->session())
        subject = req->session()->get<std::string>("sub");
    if (subject.empty() && !hintSub.empty())
        subject = hintSub;

    // Shared terminal path: notify the user's OTHER relying parties (OIDC
    // Back-Channel Logout 1.0 §2.1 counts RP-initiated logout as a logout
    // event), then respond. An empty subject or missing sessionManager skips
    // notification. Captured by copy so the synchronous error paths below
    // keep their own callback.
    auto *sessionManager = sessionManager_;
    auto finish = [sessionManager, callback](
                    std::string notifySubject,
                    ::drogon::HttpResponsePtr resp) mutable {
        if (sessionManager && !notifySubject.empty())
            sessionManager->logout(
              notifySubject,
              [callback, resp = std::move(resp)]() mutable { callback(resp); }
            );
        else
            callback(resp);
    };

    if (!postLogoutRedirectUri.empty())
    {
        // audCandidates come from the up-front verified id_token_hint decode
        // above (#88: possibly several, for a multi-audience array aud).

        // Without an id_token_hint (no client to attribute the URI to) we
        // reject immediately -- this server requires pre-registration per
        // §2.3 ("the OP MAY require pre-registration"). #88: enveloped as
        // AUTH_INVALID_ID_TOKEN_HINT (was a plain-text body), still routed
        // through finish so no backchannel notification fires.
        if (audCandidates.empty())
        {
            respondError(
              req,
              [finish](const ::drogon::HttpResponsePtr &r) mutable { finish("", r); },
              "AUTH_INVALID_ID_TOKEN_HINT",
              "end_session: post_logout_redirect_uri requires a valid id_token_hint "
              "for client identification"
            );
            return;
        }
        if (!plugin)
        {
            auto resp = ::drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(::drogon::k500InternalServerError);
            resp->setBody("OAuth2 Plugin not loaded");
            finish("", resp);
            return;
        }
        // Try each aud candidate against the client registry in order; the
        // first that owns the redirect URI wins, exhaustion is a 400 (#88:
        // enveloped as VALIDATION_REDIRECT_URI_NOT_REGISTERED, was
        // plain text). shared_ptr state: the chain outlives this stack frame
        // and re-enters itself from validateRedirectUri's async callback.
        auto candidates = std::make_shared<std::vector<std::string>>(std::move(audCandidates));
        auto tryCandidate = std::make_shared<std::function<void(size_t)>>();
        *tryCandidate =
          [req, postLogoutRedirectUri, state, finish, subject, plugin, candidates, tryCandidate](
            size_t index) mutable {
              if (index >= candidates->size())
              {
                  respondError(
                    req,
                    [finish](const ::drogon::HttpResponsePtr &r) mutable { finish("", r); },
                    "VALIDATION_REDIRECT_URI_NOT_REGISTERED",
                    "end_session: post_logout_redirect_uri is not registered for any "
                    "id_token_hint client"
                  );
                  return;
              }
              plugin->validateRedirectUri(
                (*candidates)[index],
                postLogoutRedirectUri,
                [req, postLogoutRedirectUri, state, finish, subject, index, candidates,
                 tryCandidate](bool valid) mutable {
                    if (valid)
                    {
                        // Terminate the server-side session (F-027/F-028) and
                        // notify the user's other RPs (#55).
                        if (req->session())
                            req->session()->clear();
                        std::string location = postLogoutRedirectUri;
                        if (!state.empty())
                            location += (location.find('?') == std::string::npos ? "?" : "&") +
                                        std::string("state=") +
                                        ::drogon::utils::urlEncode(state);
                        auto resp = ::drogon::HttpResponse::newRedirectionResponse(location);
                        resp->setStatusCode(::drogon::k302Found);
                        finish(subject, resp);
                        return;
                    }
                    // §2.3: this candidate does not own the URI -- fail closed
                    // across ALL candidates before answering (do NOT redirect,
                    // do NOT notify).
                    (*tryCandidate)(index + 1);
                }
              );
            };
        (*tryCandidate)(0);
        return;
    }

    // No post_logout_redirect_uri: terminate the session and return a 200 body.
    if (req->session())
        req->session()->clear();

    Json::Value json;
    json["message"] = "Logged out successfully";
    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
    resp->setStatusCode(::drogon::k200OK);
    finish(subject, resp);
}

void SessionController::registerUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto errors = ::fulla::drogon::validation::RuleSet::registerUser(req);
    if (
      ::fulla::drogon::validation::HttpResponder::respondIfErrors(errors, std::move(callback))
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

}  // namespace fulla::drogon::controllers
