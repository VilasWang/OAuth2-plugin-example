#include <authforge/drogon/controllers/AuthorizationEndpointController.h>
#include <authforge/oauth2/access/ScopeDecision.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <oauth2/validation/RuleSet.h>
#include <oauth2/validation/HttpResponder.h>
#include <oauth2/error/OAuth2ErrorHandler.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <functional>
#include <mutex>
#include <sstream>

using namespace authforge::drogon::controllers;
using namespace authforge::drogon::observability::openapi;

namespace authforge::drogon::controllers
{

::OAuth2Plugin *AuthorizationEndpointController::resolvePlugin() const
{
    return plugin_ ? plugin_ : ::drogon::app().getPlugin<::OAuth2Plugin>();
}

void AuthorizationEndpointController::initApiDocs()
{
    static std::once_flag docsOnce;
    std::call_once(docsOnce, [] { initApiDocsImpl(); });
}

void AuthorizationEndpointController::initApiDocsImpl()
{
    // Authorize endpoint
    authforge::drogon::observability::openapi::EndpointInfo authorizeEndpoint;
    authorizeEndpoint.path = "/oauth2/authorize";
    authorizeEndpoint.method = "GET";
    authorizeEndpoint.summary = "Request authorization";
    authorizeEndpoint.description = "OAuth2 authorization endpoint - initiates authorization flow";
    authorizeEndpoint.tags = {"OAuth2", "Authorization"};
    authorizeEndpoint.parameters =
      {{"client_id",
        "Client identifier (required)",
        authforge::drogon::observability::openapi::ParameterType::STRING,
        authforge::drogon::observability::openapi::ParameterLocation::QUERY,
        true},
       {"redirect_uri",
        "Redirect URI (required)",
        authforge::drogon::observability::openapi::ParameterType::STRING,
        authforge::drogon::observability::openapi::ParameterLocation::QUERY,
        true},
       {"response_type",
        "Response type, must be 'code' (required)",
        authforge::drogon::observability::openapi::ParameterType::STRING,
        authforge::drogon::observability::openapi::ParameterLocation::QUERY,
        true},
       {"scope",
        "Requested scope (optional)",
        authforge::drogon::observability::openapi::ParameterType::STRING,
        authforge::drogon::observability::openapi::ParameterLocation::QUERY,
        false},
       {"state",
        "Opaque value to maintain state between request and callback "
        "(recommended)",
        authforge::drogon::observability::openapi::ParameterType::STRING,
        authforge::drogon::observability::openapi::ParameterLocation::QUERY,
        false}};
    authorizeEndpoint
      .responses = {{302, "Redirect to client with authorization code"}, {400, "Invalid request"}};
    authorizeEndpoint.requiresAuth = false;
    OpenApiGenerator::addEndpoint(authorizeEndpoint);
}

void AuthorizationEndpointController::authorize(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    // Use ValidatorHelper for consistent validation
    auto errors = authforge::drogon::validation::RuleSet::oauth2Authorize(req);

    // Return validation errors if any
    if (authforge::drogon::validation::HttpResponder::respondIfErrors(errors, std::move(callback)))
    {
        if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
            m->incrementCounter(
              "oauth2_requests_total",
              authforge::common::ports::MetricLabels{{"endpoint", "authorize"}},
              static_cast<double>(400)
            );
        return;
    }

    auto params = req->getParameters();
    std::string responseType = params["response_type"];
    std::string clientId = params["client_id"];
    std::string redirectUri = params["redirect_uri"];
    std::string scope = params["scope"];
    std::string state = params["state"];

    // P0-4: State Parameter Enforcement
    if (state.empty())
    {
        LOG_WARN
          << "Authorization request missing state parameter (CSRF vulnerability) for client: "
          << clientId;
        if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
            m->incrementCounter(
              "oauth2_requests_total",
              authforge::common::ports::MetricLabels{{"endpoint", "authorize"}},
              static_cast<double>(400)
            );
        if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
            m->incrementCounter(
              "oauth2_login_failures_total",
              authforge::common::ports::MetricLabels{{"reason", "missing_state_parameter"}}
            );

        auto resp = ::drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(::drogon::k400BadRequest);
        resp->setBody(
          "state parameter is required for CSRF protection. "
          "Please include a state parameter in your authorization request."
        );
        callback(resp);
        return;
    }

    if (state.length() < 8 || state.length() > 512)
    {
        LOG_WARN << "Authorization request has invalid state parameter length (must be 8-512 "
                    "chars) for client: "
                 << clientId << ", state length: " << state.length();
        if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
            m->incrementCounter(
              "oauth2_requests_total",
              authforge::common::ports::MetricLabels{{"endpoint", "authorize"}},
              static_cast<double>(400)
            );
        if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
            m->incrementCounter(
              "oauth2_login_failures_total",
              authforge::common::ports::MetricLabels{{"reason", "invalid_state_parameter"}}
            );

        auto resp = ::drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(::drogon::k400BadRequest);
        resp->setBody("state parameter must be between 8 and 512 characters.");
        callback(resp);
        return;
    }

    if (
      state.find('?') != std::string::npos || state.find('#') != std::string::npos ||
      state.find('&') != std::string::npos
    )
    {
        LOG_WARN << "Authorization request has potentially malicious state parameter (contains URL "
                    "delimiters) for client: "
                 << clientId << ", state: " << state.substr(0, 20) << "...";
        if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
            m->incrementCounter(
              "oauth2_requests_total",
              authforge::common::ports::MetricLabels{{"endpoint", "authorize"}},
              static_cast<double>(400)
            );
        if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
            m->incrementCounter(
              "oauth2_login_failures_total",
              authforge::common::ports::MetricLabels{{"reason", "suspicious_state_parameter"}}
            );

        auto resp = ::drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(::drogon::k400BadRequest);
        resp->setBody("state parameter contains invalid characters.");
        callback(resp);
        return;
    }

    LOG_DEBUG << "Authorization request with valid state parameter for client: " << clientId
              << ", state: " << state.substr(0, 8) << "...";

    auto plugin = resolvePlugin();
    if (!plugin)
    {
        auto resp = ::drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(::drogon::k500InternalServerError);
        resp->setBody("OAuth2 Plugin not loaded");
        callback(resp);
        return;
    }

    // Validate Client (Async)
    plugin->validateClient(
      clientId,
      "",
      [plugin,
       clientId,
       redirectUri,
       scope,
       state,
       responseType,
       req,
       callback = std::move(callback)](bool validClient) mutable {
          if (!validClient)
          {
              if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                  m->incrementCounter(
                    "oauth2_requests_total",
                    authforge::common::ports::MetricLabels{{"endpoint", "authorize"}},
                    static_cast<double>(400)
                  );
              if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                  m->incrementCounter(
                    "oauth2_login_failures_total",
                    authforge::common::ports::MetricLabels{{"reason", "invalid_client_id"}}
                  );

              auto resp = ::drogon::HttpResponse::newHttpResponse();
              resp->setStatusCode(::drogon::k400BadRequest);
              resp->setBody("Invalid client_id");
              callback(resp);
              return;
          }

          // Validate Redirect URI (Async)
          plugin->validateRedirectUri(
            clientId,
            redirectUri,
            [plugin,
             clientId,
             redirectUri,
             scope,
             state,
             responseType,
             req,
             callback = std::move(callback)](bool validUri) mutable {
                if (!validUri)
                {
                    auto resp = ::drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(::drogon::k400BadRequest);
                    resp->setBody("Invalid redirect_uri");
                    callback(resp);
                    return;
                }

                std::vector<std::string> requestedScopes;
                std::stringstream ss(scope);
                std::string scopeItem;
                while (std::getline(ss, scopeItem, ' '))
                {
                    if (!scopeItem.empty())
                    {
                        requestedScopes.push_back(scopeItem);
                    }
                }

                // B10 / Task 45: the scope/role/consent decision is now driven
                // by the Domain-layer AuthorizationService (evaluateScopes),
                // replacing the old inline 3-tier chain
                // (validateClientScopes -> validateUserRolesForScopes ->
                // checkUserConsentAndProceed). The engine returns a single
                // ScopeValidationSummary the controller maps to a response.
                std::string userId;
                if (req->session())
                {
                    userId = req->session()->get<std::string>("userId");
                }

                if (userId.empty())
                {
                    // Not logged in -> redirect to the login screen (unchanged
                    // behavior; the engine's scope/consent tiers only apply to
                    // an authenticated subject).
                    auto customConfig = ::drogon::app().getCustomConfig();
                    std::string loginUrl = "/login";
                    if (
                      customConfig.isMember("oauth2") &&
                      customConfig["oauth2"].isMember("login_url")
                    )
                    {
                        loginUrl = customConfig["oauth2"]["login_url"].asString();
                    }
                    std::string location =
                      loginUrl + "?client_id=" + ::drogon::utils::urlEncode(clientId) +
                      "&redirect_uri=" + ::drogon::utils::urlEncode(redirectUri) +
                      "&scope=" + ::drogon::utils::urlEncode(scope) +
                      "&state=" + ::drogon::utils::urlEncode(state) +
                      "&response_type=" + ::drogon::utils::urlEncode(responseType);
                    auto resp = ::drogon::HttpResponse::newRedirectionResponse(location);
                    callback(resp);
                    return;
                }

                auto authService = plugin->getAuthorizationService();
                if (!authService)
                {
                    auto resp = ::drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(::drogon::k500InternalServerError);
                    resp->setBody("Authorization engine not available");
                    callback(resp);
                    return;
                }

                authService->evaluateScopes(
                  clientId,
                  userId,
                  requestedScopes,
                  [plugin,
                   clientId,
                   userId,
                   redirectUri,
                   state,
                   scope,
                   callback = std::move(callback)](
                    authforge::oauth2::access::ScopeValidationSummary summary
                  ) mutable {
                      if (summary.hasErrors())
                      {
                          // Tier 1 (scope_not_allowed_for_client) -> invalid_scope;
                          // Tier 2 (admin_role_required) -> access_denied.
                          std::string error = "invalid_scope";
                          std::string desc = summary.invalidReasons.empty()
                                               ? "scope validation failed"
                                               : summary.invalidReasons[0];
                          if (!desc.empty() && desc.find("admin_role") != std::string::npos)
                          {
                              error = "access_denied";
                          }
                          Json::Value jsonErr;
                          jsonErr["error"] = error;
                          jsonErr["error_description"] = desc;
                          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(jsonErr);
                          resp->setStatusCode(
                            authforge::common::error::OAuth2ErrorHandler::getHttpStatusCode(error)
                          );
                          callback(resp);
                          return;
                      }

                      if (summary.needsConsent())
                      {
                          // At least one requested scope lacks recorded consent ->
                          // route to the consent screen (unchanged redirect shape).
                          auto customConfig = ::drogon::app().getCustomConfig();
                          std::string consentUrl = "/consent";
                          if (
                            customConfig.isMember("oauth2") &&
                            customConfig["oauth2"].isMember("consent_url")
                          )
                          {
                              consentUrl = customConfig["oauth2"]["consent_url"].asString();
                          }
                          std::string location =
                            consentUrl + "?client_id=" + ::drogon::utils::urlEncode(clientId) +
                            "&user_id=" + ::drogon::utils::urlEncode(userId) +
                            "&scope=" + ::drogon::utils::urlEncode(scope) +
                            "&redirect_uri=" + ::drogon::utils::urlEncode(redirectUri) +
                            "&state=" + ::drogon::utils::urlEncode(state);
                          auto resp = ::drogon::HttpResponse::newRedirectionResponse(location);
                          callback(resp);
                          return;
                      }

                      // canProceed(): every scope Valid -> issue an auth code.
                      plugin->generateAuthorizationCode(
                        clientId,
                        userId,
                        scope,
                        redirectUri,
                        "",  // codeChallenge
                        "",  // codeChallengeMethod
                        "",  // nonce
                        [redirectUri, state, callback = std::move(callback)](
                          bool success, std::string code, std::string error
                        ) mutable {
                            if (!success)
                            {
                                LOG_ERROR << "Failed to generate authorization code: " << error;
                                Json::Value jsonErr;
                                jsonErr["error"] = "server_error";
                                jsonErr["error_description"] =
                                  "Failed to generate authorization code";
                                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(jsonErr);
                                resp->setStatusCode(::drogon::k500InternalServerError);
                                callback(resp);
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
          );
      }
    );
}

}  // namespace authforge::drogon::controllers
