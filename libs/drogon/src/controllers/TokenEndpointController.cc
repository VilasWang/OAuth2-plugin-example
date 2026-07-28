#include <authforge/drogon/controllers/TokenEndpointController.h>
#include <authforge/drogon/adapters/DrogonAuditSink.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <authforge/drogon/validation/RuleSet.h>
#include <authforge/drogon/validation/HttpResponder.h>
#include <authforge/drogon/error/OAuth2ErrorHandler.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <authforge/drogon/utils/CryptoUtils.h>
#include <authforge/oauth2/model/Client.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <algorithm>
#include <functional>
#include <mutex>
#include <sstream>

#include <authforge/storage/postgres/models/Oauth2DeviceCodes.h>

using namespace authforge::drogon::controllers;
using namespace authforge::drogon::observability::openapi;
using namespace ::drogon::orm;

namespace authforge::drogon::controllers
{

::OAuth2Plugin *TokenEndpointController::resolvePlugin() const
{
    return plugin_ ? plugin_ : ::drogon::app().getPlugin<::OAuth2Plugin>();
}

void TokenEndpointController::initApiDocs()
{
    // Explicit, order-independent registration (replaces the former file-scope
    // global object whose constructor side-effect registered these docs at
    // static-init time -> cross-TU SIOF, defect 1.1). Callers invoke this during
    // startup (plugin initAndStart / server bootstrap). A function-local
    // call_once flag makes registration happen exactly once even if invoked from
    // several call sites, so endpoints are never registered twice.
    static std::once_flag docsOnce;
    std::call_once(docsOnce, [] { initApiDocsImpl(); });
}

void TokenEndpointController::initApiDocsImpl()
{
    // Token endpoint
    {
        Json::Value successExample;
        successExample["access_token"] = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...";
        successExample["token_type"] = "Bearer";
        successExample["expires_in"] = 3600;
        successExample["refresh_token"] = "ref_123456789";
        successExample["scope"] = "openid profile";

        Json::Value errorExample;
        errorExample["error"] = "invalid_grant";
        errorExample["error_description"] = "Invalid authorization code";

        authforge::drogon::observability::openapi::EndpointInfo tokenEndpoint;
        tokenEndpoint.path = "/oauth2/token";
        tokenEndpoint.method = "POST";
        tokenEndpoint.summary = "Exchange authorization code for access token";
        tokenEndpoint.description =
          "OAuth2 token endpoint - exchanges authorization "
          "code or refresh token for access token.";
        tokenEndpoint.tags = {"OAuth2", "Token"};

        authforge::drogon::observability::openapi::ParameterInfo grantTypeParam;
        grantTypeParam.name = "grant_type";
        grantTypeParam.description = "Type of grant being requested";
        grantTypeParam.type = authforge::drogon::observability::openapi::ParameterType::STRING;
        grantTypeParam.location =
          authforge::drogon::observability::openapi::ParameterLocation::QUERY;
        grantTypeParam.required = true;
        grantTypeParam.enumValues = "authorization_code,refresh_token,client_credentials";

        authforge::drogon::observability::openapi::ParameterInfo codeParam;
        codeParam.name = "code";
        codeParam.description = "Authorization code (required for grant_type=authorization_code)";
        codeParam.type = authforge::drogon::observability::openapi::ParameterType::STRING;
        codeParam.location = authforge::drogon::observability::openapi::ParameterLocation::QUERY;
        codeParam.required = false;

        authforge::drogon::observability::openapi::ParameterInfo refreshParam;
        refreshParam.name = "refresh_token";
        refreshParam.description = "Refresh token (required for grant_type=refresh_token)";
        refreshParam.type = authforge::drogon::observability::openapi::ParameterType::STRING;
        refreshParam.location = authforge::drogon::observability::openapi::ParameterLocation::QUERY;
        refreshParam.required = false;

        authforge::drogon::observability::openapi::ParameterInfo clientIdParam;
        clientIdParam.name = "client_id";
        clientIdParam.description = "Client identifier (required)";
        clientIdParam.type = authforge::drogon::observability::openapi::ParameterType::STRING;
        clientIdParam.location =
          authforge::drogon::observability::openapi::ParameterLocation::QUERY;
        clientIdParam.required = true;

        authforge::drogon::observability::openapi::ParameterInfo clientSecretParam;
        clientSecretParam.name = "client_secret";
        clientSecretParam.description = "Client secret (required for confidential clients)";
        clientSecretParam.type = authforge::drogon::observability::openapi::ParameterType::STRING;
        clientSecretParam.location =
          authforge::drogon::observability::openapi::ParameterLocation::QUERY;
        clientSecretParam.required = true;

        authforge::drogon::observability::openapi::ParameterInfo redirectUriParam;
        redirectUriParam.name = "redirect_uri";
        redirectUriParam.description = "Redirect URI (required for authorization_code grant)";
        redirectUriParam.type = authforge::drogon::observability::openapi::ParameterType::STRING;
        redirectUriParam.location =
          authforge::drogon::observability::openapi::ParameterLocation::QUERY;
        redirectUriParam.required = false;

        tokenEndpoint.parameters =
          {grantTypeParam,
           codeParam,
           refreshParam,
           clientIdParam,
           clientSecretParam,
           redirectUriParam};
        tokenEndpoint.responses =
          {{200, "Token response with access_token and refresh_token"},
           {400, "Invalid request"},
           {401, "Authentication failed"}};
        tokenEndpoint.responseExamples = {{200, successExample}, {400, errorExample}};
        tokenEndpoint.requiresAuth = false;
        OpenApiGenerator::addEndpoint(tokenEndpoint);
    }

    // UserInfo endpoint
    {
        Json::Value successExample;
        successExample["sub"] = "1";
        successExample["name"] = "john_doe";
        successExample["email"] = "john@example.com";
        successExample["roles"] = Json::Value(Json::arrayValue);
        successExample["roles"].append("user");
        successExample["roles"].append("admin");

        Json::Value errorExample;
        errorExample["error"] = "User not found";

        authforge::drogon::observability::openapi::EndpointInfo userInfoEndpoint;
        userInfoEndpoint.path = "/oauth2/userinfo";
        userInfoEndpoint.method = "GET";
        userInfoEndpoint.summary = "Get user information";
        userInfoEndpoint.description =
          "Returns information about the authenticated user. "
          "Provides user profile data including username, email, "
          "and assigned roles according to OpenID Connect "
          "standards.";
        userInfoEndpoint.tags = {"OAuth2", "User"};
        userInfoEndpoint.parameters = {};
        userInfoEndpoint.responses =
          {{200, "User information retrieved successfully"},
           {400, "Invalid User ID format"},
           {401, "Invalid or expired access token"},
           {404, "User not found"}};
        userInfoEndpoint.responseExamples = {{200, successExample}, {404, errorExample}};
        userInfoEndpoint.requiresAuth = true;
        OpenApiGenerator::addEndpoint(userInfoEndpoint);
    }

    // Introspect endpoint
    {
        Json::Value successExample;
        successExample["active"] = true;
        successExample["client_id"] = "client_123";
        successExample["token_type"] = "Bearer";
        successExample["exp"] = 1680000000;
        successExample["sub"] = "user_456";
        successExample["scope"] = "read write";

        authforge::drogon::observability::openapi::EndpointInfo introspectEndpoint;
        introspectEndpoint.path = "/oauth2/introspect";
        introspectEndpoint.method = "POST";
        introspectEndpoint.summary = "Introspect token";
        introspectEndpoint.description =
          "RFC 7662 OAuth 2.0 Token Introspection. Returns information about a token.";
        introspectEndpoint.tags = {"OAuth2", "Token"};

        authforge::drogon::observability::openapi::ParameterInfo tokenParam;
        tokenParam.name = "token";
        tokenParam.description = "The string value of the token (required)";
        tokenParam.type = authforge::drogon::observability::openapi::ParameterType::STRING;
        tokenParam.location = authforge::drogon::observability::openapi::ParameterLocation::QUERY;
        tokenParam.required = true;

        introspectEndpoint.parameters = {tokenParam};
        introspectEndpoint.responses =
          {{200, "Token status and metadata"},
           {400, "Invalid request"},
           {401, "Authentication failed"}};
        introspectEndpoint.responseExamples = {{200, successExample}};
        introspectEndpoint.requiresAuth = true;  // Requires client credentials
        OpenApiGenerator::addEndpoint(introspectEndpoint);
    }

    // Revoke endpoint
    {
        authforge::drogon::observability::openapi::EndpointInfo revokeEndpoint;
        revokeEndpoint.path = "/oauth2/revoke";
        revokeEndpoint.method = "POST";
        revokeEndpoint.summary = "Revoke token";
        revokeEndpoint.description =
          "RFC 7009 OAuth 2.0 Token Revocation. Revokes an access or refresh token.";
        revokeEndpoint.tags = {"OAuth2", "Token"};

        authforge::drogon::observability::openapi::ParameterInfo tokenParam;
        tokenParam.name = "token";
        tokenParam.description = "The token that the client wants to get revoked (required)";
        tokenParam.type = authforge::drogon::observability::openapi::ParameterType::STRING;
        tokenParam.location = authforge::drogon::observability::openapi::ParameterLocation::QUERY;
        tokenParam.required = true;

        revokeEndpoint.parameters = {tokenParam};
        revokeEndpoint.responses =
          {{200, "Token revoked successfully or token did not exist"},
           {400, "Invalid request"},
           {401, "Authentication failed"}};
        revokeEndpoint.requiresAuth = true;  // Requires client credentials
        OpenApiGenerator::addEndpoint(revokeEndpoint);
    }
}

::drogon::HttpResponsePtr TokenEndpointController::createSuccessResponse()
{
    auto resp = ::drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(::drogon::k200OK);
    return resp;
}

ClientCredentials TokenEndpointController::extractClientCredentials(
  const ::drogon::HttpRequestPtr &req
)
{
    std::string clientId, clientSecret, authScheme;

    // Prefer HTTP Basic Auth
    auto authHeader = req->getHeader("Authorization");
    if (!authHeader.empty() && authHeader.find("Basic ") == 0)
    {
        authScheme = "Basic";
        auto basicAuth = authHeader.substr(6);
        try
        {
            auto decoded = ::drogon::utils::base64Decode(basicAuth);
            auto colonPos = decoded.find(':');
            if (colonPos != std::string::npos)
            {
                clientId = decoded.substr(0, colonPos);
                clientSecret = decoded.substr(colonPos + 1);
            }
        }
        catch (...)
        {
            LOG_ERROR << "Failed to decode Basic Auth header";
        }
    }
    else
    {
        // Fallback to POST body
        clientId = req->getParameter("client_id");
        clientSecret = req->getParameter("client_secret");
    }

    return {clientId, clientSecret, authScheme};
}

void TokenEndpointController::introspect(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    LOG_DEBUG << "Token introspection requested";

    // Extract client credentials
    auto credentials = extractClientCredentials(req);
    auto clientId = credentials.clientId;
    auto clientSecret = credentials.clientSecret;
    auto authScheme = credentials.authScheme;

    if (clientId.empty() || clientSecret.empty())
    {
        authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "invalid_client", "Client authentication required", "", authScheme
        );
        return;
    }

    // Get OAuth2 plugin
    auto plugin = resolvePlugin();
    if (!plugin)
    {
        authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "server_error", "OAuth2 plugin not available"
        );
        return;
    }

    // Validate request parameters
    auto validationErrors = authforge::drogon::validation::RuleSet::oauth2Introspect(req);
    if (!validationErrors.empty())
    {
        authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "invalid_request", validationErrors[0]
        );
        return;
    }

    // Extract token
    std::string token = req->getParameter("token");

    // Authenticate client
    plugin->validateClient(
      clientId,
      clientSecret,
      [plugin, token, clientId, authScheme, callback = std::move(callback)](bool valid) mutable {
          if (!valid)
          {
              if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                  m->incrementCounter(
                    "oauth2_introspect_errors_total",
                    authforge::common::ports::MetricLabels{
                      {"client_id", clientId}, {"error", "invalid_client"}
                    }
                  );
              authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
                std::move(callback),
                "invalid_client",
                "Client authentication failed",
                "",
                authScheme
              );
              return;
          }

          // Introspect token
          plugin->introspectToken(
            token,
            [clientId, callback = std::move(callback)](
              std::optional<authforge::oauth2::model::TokenIntrospection> introspection
            ) mutable {
                if (!introspection)
                {
                    // Token not found or invalid
                    if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                        m->incrementCounter(
                          "oauth2_introspect_requests_total",
                          authforge::common::ports::MetricLabels{{"client_id", clientId}}
                        );

                    Json::Value response;
                    response["active"] = false;
                    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(response);
                    resp->setStatusCode(::drogon::k200OK);
                    callback(resp);
                    return;
                }

                // Token is active, return full metadata
                if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                    m->incrementCounter(
                      "oauth2_introspect_requests_total",
                      authforge::common::ports::MetricLabels{{"client_id", clientId}}
                    );

                Json::Value response;
                response["active"] = introspection->active;
                response["client_id"] = introspection->clientId;
                response["token_type"] = "Bearer";

                if (introspection->exp > 0)
                {
                    response["exp"] = static_cast<Json::Int64>(introspection->exp);
                }
                if (introspection->iat > 0)
                {
                    response["iat"] = static_cast<Json::Int64>(introspection->iat);
                }
                if (introspection->nbf > 0)
                {
                    response["nbf"] = static_cast<Json::Int64>(introspection->nbf);
                }
                if (!introspection->sub.empty())
                {
                    response["sub"] = introspection->sub;
                }
                if (!introspection->aud.empty())
                {
                    response["aud"] = introspection->aud;
                }
                if (!introspection->iss.empty())
                {
                    response["iss"] = introspection->iss;
                }
                if (!introspection->scope.empty())
                {
                    response["scope"] = introspection->scope;
                }

                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(response);
                resp->setStatusCode(::drogon::k200OK);
                callback(resp);
            }
          );
      }
    );
}

void TokenEndpointController::revoke(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    LOG_DEBUG << "Token revocation requested";

    // Extract client credentials
    auto credentials = extractClientCredentials(req);
    auto clientId = credentials.clientId;
    auto clientSecret = credentials.clientSecret;
    auto authScheme = credentials.authScheme;

    if (clientId.empty() || clientSecret.empty())
    {
        authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "invalid_client", "Client authentication required", "", authScheme
        );
        return;
    }

    // Get OAuth2 plugin
    auto plugin = resolvePlugin();
    if (!plugin)
    {
        authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "server_error", "OAuth2 plugin not available"
        );
        return;
    }

    // Validate request parameters
    auto validationErrors = authforge::drogon::validation::RuleSet::oauth2Revoke(req);
    if (!validationErrors.empty())
    {
        authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "invalid_request", validationErrors[0]
        );
        return;
    }

    // Extract token
    std::string token = req->getParameter("token");

    // Authenticate client
    plugin->validateClient(
      clientId,
      clientSecret,
      [plugin, token, clientId, authScheme, callback = std::move(callback)](bool valid) mutable {
          if (!valid)
          {
              if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                  m->incrementCounter(
                    "oauth2_revocation_errors_total",
                    authforge::common::ports::MetricLabels{
                      {"client_id", clientId}, {"error", "invalid_client"}
                    }
                  );
              authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
                std::move(callback),
                "invalid_client",
                "Client authentication failed",
                "",
                authScheme
              );
              return;
          }

          // Check token ownership (permission control)
          plugin->introspectToken(
            token,
            [plugin, token, clientId, callback = std::move(callback)](
              std::optional<authforge::oauth2::model::TokenIntrospection> introspection
            ) mutable {
                if (!introspection || !introspection->active)
                {
                    // Token doesn't exist or inactive - return success per RFC 7009
                    // (prevents token probing attacks)
                    if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                        m->incrementCounter(
                          "oauth2_revocation_requests_total",
                          authforge::common::ports::MetricLabels{{"client_id", clientId}}
                        );
                    callback(createSuccessResponse());
                    return;
                }

                // Check permission: only token owner can revoke
                if (introspection->clientId != clientId)
                {
                    if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                        m->incrementCounter(
                          "oauth2_revocation_errors_total",
                          authforge::common::ports::MetricLabels{
                            {"client_id", clientId}, {"error", "unauthorized_client"}
                          }
                        );
                    authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
                      std::move(callback),
                      "unauthorized_client",
                      "This client is not allowed to revoke the token"
                    );
                    return;
                }

                // Has permission, execute revocation
                plugin->revokeAccessToken(
                  token, clientId, [clientId, callback = std::move(callback), token]() mutable {
                      ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                        ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                        "token_revoked",
                        "success",
                        nullptr,
                        clientId,
                        "token",
                        token
                      );
                      if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                          m->incrementCounter(
                            "oauth2_revocation_requests_total",
                            authforge::common::ports::MetricLabels{{"client_id", clientId}}
                          );
                      callback(createSuccessResponse());
                  }
                );
            }
          );
      }
    );
}

void TokenEndpointController::token(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    // Use ValidatorHelper for consistent validation
    auto errors = authforge::drogon::validation::RuleSet::oauth2Token(req);

    // Return validation errors if any
    if (authforge::drogon::validation::HttpResponder::respondIfErrors(errors, std::move(callback)))
    {
        if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
            m->incrementCounter(
              "oauth2_requests_total",
              authforge::common::ports::MetricLabels{{"endpoint", "token"}},
              static_cast<double>(400)
            );
        return;
    }

    auto plugin = resolvePlugin();
    if (!plugin)
    {
        auto resp = ::drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(::drogon::k500InternalServerError);
        resp->setBody("OAuth2 Plugin not loaded");
        callback(resp);
        return;
    }

    std::string grantType, code, redirectUri, clientId, clientSecret;
    std::string refreshToken;
    std::string codeVerifier;

    std::string authHeader = req->getHeader("Authorization");
    if (!authHeader.empty() && authHeader.substr(0, 6) == "Basic ")
    {
        LOG_DEBUG << "Token endpoint: Attempting HTTP Basic Authentication";
        try
        {
            std::string decoded = ::drogon::utils::base64Decode(authHeader.substr(6));
            size_t colonPos = decoded.find(':');
            if (colonPos != std::string::npos)
            {
                clientId = decoded.substr(0, colonPos);
                clientSecret = decoded.substr(colonPos + 1);
                LOG_DEBUG << "Token endpoint: Parsed Basic Auth for client_id=" << clientId;
            }
            else
            {
                LOG_WARN << "Token endpoint: Invalid Basic Auth format (missing colon)";
            }
        }
        catch (const std::exception &e)
        {
            LOG_WARN << "Token endpoint: Base64 decode failed - " << e.what();
        }
    }

    if (clientId.empty() || req->method() == ::drogon::Post)
    {
        auto params = req->getParameters();
        if (clientId.empty())
            clientId = params["client_id"];
        if (clientSecret.empty())
            clientSecret = params["client_secret"];
        grantType = params["grant_type"];
        code = params["code"];
        redirectUri = params["redirect_uri"];
        refreshToken = params["refresh_token"];
        codeVerifier = params["code_verifier"];
    }
    else
    {
        if (clientId.empty())
            clientId = req->getParameter("client_id");
        if (clientSecret.empty())
            clientSecret = req->getParameter("client_secret");
        grantType = req->getParameter("grant_type");
        code = req->getParameter("code");
        redirectUri = req->getParameter("redirect_uri");
        refreshToken = req->getParameter("refresh_token");
        codeVerifier = req->getParameter("code_verifier");
    }

    if (grantType == "authorization_code")
    {
        plugin->exchangeCodeForToken(
          code,
          clientId,
          clientSecret,
          redirectUri,
          codeVerifier,
          [callback = std::move(callback)](const Json::Value &result) {
              if (result.isMember("error"))
              {
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(result);
                  std::string errorCode = result.get("error", "").asString();
                  ::drogon::HttpStatusCode statusCode =
                    authforge::common::error::OAuth2ErrorHandler::getHttpStatusCode(errorCode);
                  resp->setStatusCode(statusCode);
                  if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                      m->incrementCounter(
                        "oauth2_requests_total",
                        authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                        static_cast<double>(static_cast<int>(statusCode))
                      );
                  callback(resp);
                  return;
              }

              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(result);
              if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                  m->incrementCounter(
                    "oauth2_requests_total",
                    authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                    static_cast<double>(200)
                  );
              if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                  m->setGauge(
                    "oauth2_active_tokens",
                    authforge::common::ports::MetricLabels{},
                    static_cast<double>(1)
                  );
              callback(resp);
          }
        );
    }
    else if (grantType == "refresh_token")
    {
        std::string refreshTokenStr = refreshToken;
        plugin->refreshAccessToken(
          refreshTokenStr, clientId, [callback = std::move(callback)](const Json::Value &result) {
              if (result.isMember("error"))
              {
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(result);
                  std::string errorCode = result.get("error", "").asString();
                  ::drogon::HttpStatusCode statusCode =
                    authforge::common::error::OAuth2ErrorHandler::getHttpStatusCode(errorCode);
                  resp->setStatusCode(statusCode);
                  if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                      m->incrementCounter(
                        "oauth2_requests_total",
                        authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                        static_cast<double>(static_cast<int>(statusCode))
                      );
                  callback(resp);
                  return;
              }

              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(result);
              if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                  m->incrementCounter(
                    "oauth2_requests_total",
                    authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                    static_cast<double>(200)
                  );
              callback(resp);
          }
        );
    }
    else if (grantType == "client_credentials")
    {
        // Client Credentials Grant (RFC 6749 Section 4.4)
        // Only CONFIDENTIAL clients can use this grant type
        if (clientId.empty() || clientSecret.empty())
        {
            Json::Value error;
            error["error"] = "invalid_client";
            error["error_description"] =
              "Client authentication required for client_credentials grant";
            auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(::drogon::k401Unauthorized);
            callback(resp);
            return;
        }

        auto sharedCb = std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
          std::move(callback)
        );

        plugin
          ->validateClient(clientId, clientSecret, [plugin, clientId, req, sharedCb](bool valid) {
              if (!valid)
              {
                  Json::Value error;
                  error["error"] = "invalid_client";
                  error["error_description"] = "Client authentication failed";
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(::drogon::k401Unauthorized);
                  (*sharedCb)(resp);
                  return;
              }

              // Verify client is CONFIDENTIAL (PUBLIC clients cannot use client_credentials)
              // Phase 4.3: route through plugin->getClient (NEW IClientRepository
              // via the bridge) instead of getStorage()->getClient. The plugin
              // outlives this request (it is a config-driven singleton), so no
              // shared_ptr capture of storage is needed across this async hop.
              plugin->getClient(
                clientId,
                [plugin, clientId, req, sharedCb](
                  std::optional<authforge::oauth2::model::OAuth2Client> client
                ) {
                    if (!client)
                    {
                        Json::Value error;
                        error["error"] = "invalid_client";
                        auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(::drogon::k401Unauthorized);
                        (*sharedCb)(resp);
                        return;
                    }

                    if (client->clientType == authforge::oauth2::model::ClientType::PUBLIC)
                    {
                        Json::Value error;
                        error["error"] = "unauthorized_client";
                        error["error_description"] =
                          "Public clients cannot use client_credentials grant";
                        auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(::drogon::k401Unauthorized);
                        (*sharedCb)(resp);
                        return;
                    }

                    // P0 #2 (评审问题点 2, RFC 6749 §3.3): validate the requested
                    // scope against the client's registered allowlist instead of
                    // echoing it back unchecked (or hardcoding "read" as default).
                    // - requested scope exceeding the allowlist -> invalid_scope
                    // - omitted scope -> default to the full registered scope set
                    // - omitted scope + empty registration -> invalid_scope (the
                    //   server has no pre-defined default to fall back on)
                    authforge::oauth2::model::Client aggregate(*client);
                    std::string requestedScope = req->getParameter("scope");
                    std::string grantedScope;
                    if (!requestedScope.empty())
                    {
                        if (!aggregate.allowsAllScopes(requestedScope))
                        {
                            Json::Value error;
                            error["error"] = "invalid_scope";
                            error["error_description"] =
                              "Requested scope exceeds the scopes registered for this client";
                            auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
                            resp->setStatusCode(::drogon::k400BadRequest);
                            (*sharedCb)(resp);
                            return;
                        }
                        grantedScope = requestedScope;
                    }
                    else
                    {
                        const auto &allowed = aggregate.allowedScopes();
                        if (allowed.empty())
                        {
                            Json::Value error;
                            error["error"] = "invalid_scope";
                            error["error_description"] =
                              "No scope requested and no default scope registered for this "
                              "client";
                            auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
                            resp->setStatusCode(::drogon::k400BadRequest);
                            (*sharedCb)(resp);
                            return;
                        }
                        for (const auto &s : allowed)
                        {
                            if (!grantedScope.empty())
                                grantedScope += " ";
                            grantedScope += s;
                        }
                    }

                    // Generate access token (no refresh token for client_credentials)
                    auto tokenStr = authforge::drogon::utils::generateSecureToken();
                    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch()
                    )
                                 .count();

                    authforge::oauth2::model::OAuth2AccessToken token;
                    token.token = authforge::drogon::utils::hashToken(tokenStr);
                    token.clientId = clientId;
                    token.userId = "client:" + clientId;  // M2M: subject is the client itself
                    token.scope = grantedScope;
                    token.expiresAt = now + 3600;

                    // Phase 4.3: route through plugin->saveAccessToken (NEW
                    // ITokenRepository) instead of getStorage()->saveAccessToken.
                    plugin->saveAccessToken(token, [sharedCb, tokenStr, grantedScope]() {
                        Json::Value json;
                        json["access_token"] = tokenStr;
                        json["token_type"] = "Bearer";
                        json["expires_in"] = 3600;
                        json["scope"] = grantedScope;
                        // No refresh_token for client_credentials
                        auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                        if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                            m->incrementCounter(
                              "oauth2_requests_total",
                              authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                              static_cast<double>(200)
                            );
                        (*sharedCb)(resp);
                    });
                }
              );
          });
    }
    else if (grantType == "urn:ietf:params:oauth:grant-type:device_code")
    {
        // Device Authorization Grant (RFC 8628)
        std::string deviceCode = req->getParameter("device_code");
        if (clientId.empty())
        {
            clientId = req->getParameter("client_id");
        }

        if (deviceCode.empty() || clientId.empty())
        {
            Json::Value error;
            error["error"] = "invalid_request";
            error["error_description"] = "device_code and client_id are required";
            auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(::drogon::k400BadRequest);
            if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                m->incrementCounter(
                  "oauth2_requests_total",
                  authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                  static_cast<double>(400)
                );
            callback(resp);
            return;
        }

        std::string deviceCodeHash = authforge::drogon::utils::hashToken(deviceCode);

        auto dbClient = ::drogon::app().getDbClient();
        if (!dbClient)
        {
            Json::Value error;
            error["error"] = "server_error";
            error["error_description"] = "Database not available";
            auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(::drogon::k500InternalServerError);
            callback(resp);
            return;
        }

        auto sharedCb = std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
          std::move(callback)
        );

        Mapper<drogon_model::oauth2_db::Oauth2DeviceCodes> mapper(dbClient);
        mapper.findBy(
          Criteria(
            drogon_model::oauth2_db::Oauth2DeviceCodes::Cols::_device_code_hash,
            CompareOperator::EQ,
            deviceCodeHash
          ),
          [plugin, sharedCb, clientId, deviceCodeHash](
            const std::vector<drogon_model::oauth2_db::Oauth2DeviceCodes> &results
          ) {
              if (results.empty())
              {
                  Json::Value error;
                  error["error"] = "invalid_grant";
                  error["error_description"] = "Invalid device_code";
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(::drogon::k400BadRequest);
                  if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                      m->incrementCounter(
                        "oauth2_requests_total",
                        authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                        static_cast<double>(400)
                      );
                  (*sharedCb)(resp);
                  return;
              }

              const auto &row = results[0];
              std::string storedClientId = row.getValueOfClientId();
              std::string status = row.getValueOfStatus();
              int64_t expiresAt = row.getValueOfExpiresAt();
              std::string scope = row.getValueOfScope();
              std::string userId = row.getValueOfUserId();

              // Verify client_id matches
              if (storedClientId != clientId)
              {
                  Json::Value error;
                  error["error"] = "invalid_grant";
                  error["error_description"] = "client_id mismatch";
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(::drogon::k400BadRequest);
                  if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                      m->incrementCounter(
                        "oauth2_requests_total",
                        authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                        static_cast<double>(400)
                      );
                  (*sharedCb)(resp);
                  return;
              }

              // Check expiration
              auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch()
              )
                           .count();
              if (now >= expiresAt)
              {
                  Json::Value error;
                  error["error"] = "expired_token";
                  error["error_description"] = "The device_code has expired";
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(::drogon::k400BadRequest);
                  if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                      m->incrementCounter(
                        "oauth2_requests_total",
                        authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                        static_cast<double>(400)
                      );
                  (*sharedCb)(resp);
                  return;
              }

              // Check status
              if (status == "pending")
              {
                  Json::Value error;
                  error["error"] = "authorization_pending";
                  error["error_description"] = "The authorization request is still pending";
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(::drogon::k400BadRequest);
                  if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                      m->incrementCounter(
                        "oauth2_requests_total",
                        authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                        static_cast<double>(400)
                      );
                  (*sharedCb)(resp);
                  return;
              }

              if (status == "denied")
              {
                  Json::Value error;
                  error["error"] = "access_denied";
                  error["error_description"] = "The user denied the authorization request";
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(::drogon::k400BadRequest);
                  if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                      m->incrementCounter(
                        "oauth2_requests_total",
                        authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                        static_cast<double>(400)
                      );
                  (*sharedCb)(resp);
                  return;
              }

              if (status != "approved")
              {
                  Json::Value error;
                  error["error"] = "invalid_grant";
                  error["error_description"] = "Invalid device code status";
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(::drogon::k400BadRequest);
                  if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                      m->incrementCounter(
                        "oauth2_requests_total",
                        authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                        static_cast<double>(400)
                      );
                  (*sharedCb)(resp);
                  return;
              }

              // Status is "approved" �?issue tokens
              auto accessTokenStr = authforge::drogon::utils::generateSecureToken();
              auto refreshTokenStr = authforge::drogon::utils::generateSecureToken();
              std::string familyId = authforge::drogon::utils::generateSecureToken(16);

              authforge::oauth2::model::OAuth2AccessToken accessToken;
              accessToken.token = authforge::drogon::utils::hashToken(accessTokenStr);
              accessToken.clientId = clientId;
              accessToken.userId = userId;
              accessToken.scope = scope;
              accessToken.issuedAt = now;
              accessToken.expiresAt = now + 3600;

              authforge::oauth2::model::OAuth2RefreshToken refreshToken;
              refreshToken.token = authforge::drogon::utils::hashToken(refreshTokenStr);
              refreshToken.accessToken = accessToken.token;
              refreshToken.clientId = clientId;
              refreshToken.userId = userId;
              refreshToken.scope = scope;
              refreshToken.expiresAt = now + (3600 * 24 * 30);
              refreshToken.familyId = familyId;

              // Phase 4.3: route through plugin->saveTokenPair (NEW
              // ITokenRepository) instead of getStorage()->saveTokenPair.
              plugin->saveTokenPair(
                accessToken,
                refreshToken,
                [sharedCb, accessTokenStr, refreshTokenStr, scope, deviceCodeHash]() {
                    // Mark device code as consumed by deleting it
                    auto dbClient = ::drogon::app().getDbClient();
                    if (dbClient)
                    {
                        Mapper<drogon_model::oauth2_db::Oauth2DeviceCodes>(dbClient).deleteBy(
                          Criteria(
                            drogon_model::oauth2_db::Oauth2DeviceCodes::Cols::_device_code_hash,
                            CompareOperator::EQ,
                            deviceCodeHash
                          ),
                          [](const size_t) {},
                          [](const ::drogon::orm::DrogonDbException &e) {
                              LOG_WARN << "Failed to delete consumed device code: "
                                       << e.base().what();
                          }
                        );
                    }

                    Json::Value json;
                    json["access_token"] = accessTokenStr;
                    json["token_type"] = "Bearer";
                    json["expires_in"] = 3600;
                    json["refresh_token"] = refreshTokenStr;
                    if (!scope.empty())
                    {
                        json["scope"] = scope;
                    }

                    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                    if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                        m->incrementCounter(
                          "oauth2_requests_total",
                          authforge::common::ports::MetricLabels{{"endpoint", "token"}},
                          static_cast<double>(200)
                        );
                    if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
                        m->setGauge(
                          "oauth2_active_tokens",
                          authforge::common::ports::MetricLabels{},
                          static_cast<double>(1)
                        );
                    (*sharedCb)(resp);
                }
              );
          },
          [sharedCb](const ::drogon::orm::DrogonDbException &e) {
              LOG_ERROR << "Device code lookup failed: " << e.base().what();
              Json::Value error;
              error["error"] = "server_error";
              error["error_description"] = "Failed to process device code";
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(::drogon::k500InternalServerError);
              (*sharedCb)(resp);
          }
        );
    }
    else
    {
        Json::Value error;
        error["error"] = "unsupported_grant_type";
        error["error_description"] =
          "Supported types: authorization_code, refresh_token, client_credentials, "
          "urn:ietf:params:oauth:grant-type:device_code";
        auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(::drogon::k400BadRequest);
        if (auto m = ::drogon::app().getPlugin<::OAuth2Plugin>()->getMetrics())
            m->incrementCounter(
              "oauth2_requests_total",
              authforge::common::ports::MetricLabels{{"endpoint", "token"}},
              static_cast<double>(400)
            );
        callback(resp);
    }
}

void TokenEndpointController::userInfo(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    if (req->method() == ::drogon::Options)
    {
        auto resp = ::drogon::HttpResponse::newHttpResponse();
        callback(resp);
        return;
    }

    std::string userId;
    auto attrs = req->getAttributes();
    if (!attrs->find("userId"))
    {
        auto resp = ::drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(::drogon::k401Unauthorized);
        resp->setBody("User ID not found in request attributes");
        callback(resp);
        return;
    }
    userId = attrs->get<std::string>("userId");

    auto plugin = resolvePlugin();
    if (!plugin)
    {
        Json::Value userInfo;
        userInfo["sub"] = userId;
        auto resp = ::drogon::HttpResponse::newHttpJsonResponse(userInfo);
        callback(resp);
        return;
    }
    // First get user roles
    plugin->getUserRoles(userId, [this, userId, callback](std::vector<std::string> roles) {
        // Phase 4.5: route through plugin->getUserInfo (today still the god
        // facade; the identity-side migration to authforge::identity::* is a
        // separate follow-up). No getStorage() reach-in.
        auto plugin = resolvePlugin();
        plugin
          ->getUserInfo(userId, [userId, roles, callback](std::optional<Json::Value> dbUserInfo) {
              Json::Value userInfo;
              userInfo["sub"] = userId;

              // Build OIDC claims from storage result.
              // storage->getUserInfo returns {id, username?, email?} with no name field,
              // so compute the 'name' claim here: username preferred, fallback to email
              // (username is optional in the email-first model) so strict OIDC clients
              // never see an empty/missing name.
              if (dbUserInfo)
              {
                  std::string uname =
                    dbUserInfo->isMember("username") ? (*dbUserInfo)["username"].asString() : "";
                  std::string email =
                    dbUserInfo->isMember("email") ? (*dbUserInfo)["email"].asString() : "";
                  userInfo["name"] = uname.empty() ? email : uname;  // OpenID Connect 'name' claim
                  if (!uname.empty())
                  {
                      userInfo["username"] = uname;
                  }
                  if (!email.empty())
                  {
                      userInfo["email"] = email;
                  }
              }
              else
              {
                  // Fallback to using userId as name
                  userInfo["username"] = userId;
                  userInfo["name"] = userId;
              }

              // Add roles
              if (!roles.empty())
              {
                  userInfo["roles"] = Json::Value(Json::arrayValue);
                  for (const auto &role : roles)
                  {
                      userInfo["roles"].append(role);
                  }
              }

              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(userInfo);
              callback(resp);
          });
    });
}

}  // namespace authforge::drogon::controllers
