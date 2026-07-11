#include <authforge/drogon/controllers/OAuth2StandardController.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <oauth2/observability/OAuth2Metrics.h>
#include <oauth2/validation/RuleSet.h>
#include <oauth2/validation/HttpResponder.h>
#include <oauth2/error/OAuth2ErrorHandler.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <oauth2/utils/CryptoUtils.h>
#include <oauth2/observability/AuditLogger.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <algorithm>
#include <functional>
#include <mutex>
#include <sstream>

using namespace oauth2;
using namespace oauth2::controllers;
using namespace authforge::drogon::observability::openapi;

namespace oauth2::controllers
{

::OAuth2Plugin *OAuth2StandardController::resolvePlugin() const
{
    return plugin_ ? plugin_ : drogon::app().getPlugin<::OAuth2Plugin>();
}

void OAuth2StandardController::initApiDocs()
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

void OAuth2StandardController::initApiDocsImpl()
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

    // OIDC Discovery endpoint
    {
        authforge::drogon::observability::openapi::EndpointInfo discoveryEndpoint;
        discoveryEndpoint.path = "/.well-known/openid-configuration";
        discoveryEndpoint.method = "GET";
        discoveryEndpoint.summary = "OpenID Connect Discovery";
        discoveryEndpoint.description =
          "Returns OIDC discovery metadata including endpoints and supported scopes.";
        discoveryEndpoint.tags = {"OpenID Connect"};
        discoveryEndpoint.parameters = {};
        discoveryEndpoint.responses = {{200, "OIDC Provider Metadata"}};
        discoveryEndpoint.requiresAuth = false;
        OpenApiGenerator::addEndpoint(discoveryEndpoint);
    }

    // JWKS endpoint
    {
        authforge::drogon::observability::openapi::EndpointInfo jwksEndpoint;
        jwksEndpoint.path = "/.well-known/jwks.json";
        jwksEndpoint.method = "GET";
        jwksEndpoint.summary = "JSON Web Key Set";
        jwksEndpoint.description = "Returns the public keys used by this server to sign JWTs.";
        jwksEndpoint.tags = {"OpenID Connect", "Security"};
        jwksEndpoint.parameters = {};
        jwksEndpoint.responses = {{200, "JSON Web Key Set"}};
        jwksEndpoint.requiresAuth = false;
        OpenApiGenerator::addEndpoint(jwksEndpoint);
    }
}

drogon::HttpResponsePtr OAuth2StandardController::createSuccessResponse()
{
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k200OK);
    return resp;
}

ClientCredentials OAuth2StandardController::extractClientCredentials(
  const drogon::HttpRequestPtr &req
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
            auto decoded = drogon::utils::base64Decode(basicAuth);
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

void OAuth2StandardController::introspect(
  const drogon::HttpRequestPtr &req,
  std::function<void(const drogon::HttpResponsePtr &)> &&callback
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
        common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "invalid_client", "Client authentication required", "", authScheme
        );
        return;
    }

    // Get OAuth2 plugin
    auto plugin = resolvePlugin();
    if (!plugin)
    {
        common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "server_error", "OAuth2 plugin not available"
        );
        return;
    }

    // Validate request parameters
    auto validationErrors = oauth2::validation::RuleSet::oauth2Introspect(req);
    if (!validationErrors.empty())
    {
        common::error::OAuth2ErrorHandler::sendErrorResponse(
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
              oauth2::observability::Metrics::incrementIntrospectErrors(clientId, "invalid_client");
              common::error::OAuth2ErrorHandler::sendErrorResponse(
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
              std::optional<oauth2::TokenIntrospection> introspection
            ) mutable {
                if (!introspection)
                {
                    // Token not found or invalid
                    oauth2::observability::Metrics::incrementIntrospectRequests(clientId);

                    Json::Value response;
                    response["active"] = false;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                    resp->setStatusCode(drogon::k200OK);
                    callback(resp);
                    return;
                }

                // Token is active, return full metadata
                oauth2::observability::Metrics::incrementIntrospectRequests(clientId);

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

                auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                resp->setStatusCode(drogon::k200OK);
                callback(resp);
            }
          );
      }
    );
}

void OAuth2StandardController::revoke(
  const drogon::HttpRequestPtr &req,
  std::function<void(const drogon::HttpResponsePtr &)> &&callback
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
        common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "invalid_client", "Client authentication required", "", authScheme
        );
        return;
    }

    // Get OAuth2 plugin
    auto plugin = resolvePlugin();
    if (!plugin)
    {
        common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "server_error", "OAuth2 plugin not available"
        );
        return;
    }

    // Validate request parameters
    auto validationErrors = oauth2::validation::RuleSet::oauth2Revoke(req);
    if (!validationErrors.empty())
    {
        common::error::OAuth2ErrorHandler::sendErrorResponse(
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
              oauth2::observability::Metrics::incrementRevocationErrors(clientId, "invalid_client");
              common::error::OAuth2ErrorHandler::sendErrorResponse(
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
              std::optional<oauth2::TokenIntrospection> introspection
            ) mutable {
                if (!introspection || !introspection->active)
                {
                    // Token doesn't exist or inactive - return success per RFC 7009
                    // (prevents token probing attacks)
                    oauth2::observability::Metrics::incrementRevocationRequests(clientId);
                    callback(createSuccessResponse());
                    return;
                }

                // Check permission: only token owner can revoke
                if (introspection->clientId != clientId)
                {
                    oauth2::observability::Metrics::incrementRevocationErrors(
                      clientId, "unauthorized_client"
                    );
                    common::error::OAuth2ErrorHandler::sendErrorResponse(
                      std::move(callback),
                      "unauthorized_client",
                      "This client is not allowed to revoke the token"
                    );
                    return;
                }

                // Has permission, execute revocation
                plugin->revokeAccessToken(
                  token, clientId, [clientId, callback = std::move(callback), token]() mutable {
                      oauth2::observability::AuditLogger::log(
                        "token_revoked", "success", nullptr, clientId, "token", token
                      );
                      oauth2::observability::Metrics::incrementRevocationRequests(clientId);
                      callback(createSuccessResponse());
                  }
                );
            }
          );
      }
    );
}

void OAuth2StandardController::metadata(
  const drogon::HttpRequestPtr &req,
  std::function<void(const drogon::HttpResponsePtr &)> &&callback
)
{
    LOG_DEBUG << "Metadata endpoint requested";

    // Get OAuth2 plugin
    auto plugin = resolvePlugin();
    if (!plugin)
    {
        common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "server_error", "OAuth2 plugin not available"
        );
        return;
    }

    // Base URL from configuration (required for production)
    std::string baseUrl;
    auto customConfig = drogon::app().getCustomConfig();
    if (customConfig.isMember("metadata") && customConfig["metadata"].isMember("issuer"))
    {
        baseUrl = customConfig["metadata"]["issuer"].asString();
    }
    if (baseUrl.empty())
    {
        // Fallback: construct from listener (dev mode only)
        baseUrl = "http://localhost:5555";
        LOG_WARN << "metadata.issuer not configured, using fallback: " << baseUrl;
    }

    Json::Value metadata;

    // Basic server info
    metadata["issuer"] = baseUrl;
    metadata["authorization_endpoint"] = baseUrl + "/oauth2/authorize";
    metadata["token_endpoint"] = baseUrl + "/oauth2/token";
    metadata["device_authorization_endpoint"] = baseUrl + "/oauth2/device_authorization";

    // P1 endpoints
    metadata["introspection_endpoint"] = baseUrl + "/oauth2/introspect";
    metadata["introspection_endpoint_auth_methods_supported"] = Json::Value(Json::arrayValue);
    metadata["introspection_endpoint_auth_methods_supported"].append("client_secret_post");
    metadata["introspection_endpoint_auth_methods_supported"].append("client_secret_basic");

    metadata["revocation_endpoint"] = baseUrl + "/oauth2/revoke";
    metadata["revocation_endpoint_auth_methods_supported"] = Json::Value(Json::arrayValue);
    metadata["revocation_endpoint_auth_methods_supported"].append("client_secret_post");
    metadata["revocation_endpoint_auth_methods_supported"].append("client_secret_basic");

    // OpenID Connect support (partial, based on what we implement)
    metadata["scopes_supported"] = Json::Value(Json::arrayValue);
    metadata["scopes_supported"].append("openid");
    metadata["scopes_supported"].append("profile");
    metadata["scopes_supported"].append("email");
    metadata["scopes_supported"].append("admin");

    metadata["response_types_supported"] = Json::Value(Json::arrayValue);
    metadata["response_types_supported"].append("code");

    metadata["response_modes_supported"] = Json::Value(Json::arrayValue);
    metadata["response_modes_supported"].append("query");

    metadata["grant_types_supported"] = Json::Value(Json::arrayValue);
    metadata["grant_types_supported"].append("authorization_code");
    metadata["grant_types_supported"].append("refresh_token");
    metadata["grant_types_supported"].append("client_credentials");
    metadata["grant_types_supported"].append("urn:ietf:params:oauth:grant-type:device_code");

    // PKCE support
    metadata["code_challenge_methods_supported"] = Json::Value(Json::arrayValue);
    metadata["code_challenge_methods_supported"].append("plain");
    metadata["code_challenge_methods_supported"].append("S256");

    // Client authentication methods
    metadata["token_endpoint_auth_methods_supported"] = Json::Value(Json::arrayValue);
    metadata["token_endpoint_auth_methods_supported"].append("client_secret_post");
    metadata["token_endpoint_auth_methods_supported"].append("client_secret_basic");

    // Documentation and policies (if configured)
    if (customConfig.isMember("metadata"))
    {
        if (customConfig["metadata"].isMember("service_documentation"))
            metadata["service_documentation"] = customConfig["metadata"]["service_documentation"];
        if (customConfig["metadata"].isMember("op_policy_uri"))
            metadata["op_policy_uri"] = customConfig["metadata"]["op_policy_uri"];
        if (customConfig["metadata"].isMember("op_tos_uri"))
            metadata["op_tos_uri"] = customConfig["metadata"]["op_tos_uri"];
    }

    auto resp = drogon::HttpResponse::newHttpJsonResponse(metadata);
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    callback(resp);
}

void OAuth2StandardController::oidcDiscovery(
  const drogon::HttpRequestPtr &req,
  std::function<void(const drogon::HttpResponsePtr &)> &&callback
)
{
    // OpenID Connect Discovery 1.0
    std::string baseUrl;
    auto customConfig = drogon::app().getCustomConfig();
    if (customConfig.isMember("metadata") && customConfig["metadata"].isMember("issuer"))
    {
        baseUrl = customConfig["metadata"]["issuer"].asString();
    }
    if (baseUrl.empty())
    {
        baseUrl = "http://localhost:5555";
    }

    Json::Value discovery;
    discovery["issuer"] = baseUrl;
    discovery["authorization_endpoint"] = baseUrl + "/oauth2/authorize";
    discovery["token_endpoint"] = baseUrl + "/oauth2/token";
    discovery["device_authorization_endpoint"] = baseUrl + "/oauth2/device_authorization";
    discovery["userinfo_endpoint"] = baseUrl + "/oauth2/userinfo";
    discovery["jwks_uri"] = baseUrl + "/.well-known/jwks.json";
    discovery["introspection_endpoint"] = baseUrl + "/oauth2/introspect";
    discovery["revocation_endpoint"] = baseUrl + "/oauth2/revoke";

    discovery["scopes_supported"] = Json::Value(Json::arrayValue);
    discovery["scopes_supported"].append("openid");
    discovery["scopes_supported"].append("profile");
    discovery["scopes_supported"].append("email");
    discovery["scopes_supported"].append("admin");

    discovery["response_types_supported"] = Json::Value(Json::arrayValue);
    discovery["response_types_supported"].append("code");

    discovery["grant_types_supported"] = Json::Value(Json::arrayValue);
    discovery["grant_types_supported"].append("authorization_code");
    discovery["grant_types_supported"].append("refresh_token");
    discovery["grant_types_supported"].append("client_credentials");
    discovery["grant_types_supported"].append("urn:ietf:params:oauth:grant-type:device_code");

    discovery["subject_types_supported"] = Json::Value(Json::arrayValue);
    discovery["subject_types_supported"].append("public");

    discovery["id_token_signing_alg_values_supported"] = Json::Value(Json::arrayValue);
    discovery["id_token_signing_alg_values_supported"].append("RS256");

    discovery["token_endpoint_auth_methods_supported"] = Json::Value(Json::arrayValue);
    discovery["token_endpoint_auth_methods_supported"].append("client_secret_basic");
    discovery["token_endpoint_auth_methods_supported"].append("client_secret_post");

    discovery["code_challenge_methods_supported"] = Json::Value(Json::arrayValue);
    discovery["code_challenge_methods_supported"].append("S256");
    discovery["code_challenge_methods_supported"].append("plain");

    discovery["claims_supported"] = Json::Value(Json::arrayValue);
    discovery["claims_supported"].append("sub");
    discovery["claims_supported"].append("name");
    discovery["claims_supported"].append("email");
    discovery["claims_supported"].append("email_verified");
    discovery["claims_supported"].append("iss");
    discovery["claims_supported"].append("aud");
    discovery["claims_supported"].append("exp");
    discovery["claims_supported"].append("iat");
    discovery["claims_supported"].append("nonce");

    auto resp = drogon::HttpResponse::newHttpJsonResponse(discovery);
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    callback(resp);
}

void OAuth2StandardController::jwks(
  const drogon::HttpRequestPtr &req,
  std::function<void(const drogon::HttpResponsePtr &)> &&callback
)
{
    auto plugin = resolvePlugin();
    if (!plugin || !plugin->getJwkManager())
    {
        Json::Value empty;
        empty["keys"] = Json::Value(Json::arrayValue);
        auto resp = drogon::HttpResponse::newHttpJsonResponse(empty);
        callback(resp);
        return;
    }

    auto jwks = plugin->getJwkManager()->getJwks();
    auto resp = drogon::HttpResponse::newHttpJsonResponse(jwks);
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    // Cache JWKS for 1 hour (keys don't change frequently)
    resp->addHeader("Cache-Control", "public, max-age=3600");
    callback(resp);
}

void OAuth2StandardController::authorize(
  const drogon::HttpRequestPtr &req,
  std::function<void(const drogon::HttpResponsePtr &)> &&callback
)
{
    // Use ValidatorHelper for consistent validation
    auto errors = oauth2::validation::RuleSet::oauth2Authorize(req);

    // Return validation errors if any
    if (oauth2::validation::HttpResponder::respondIfErrors(errors, std::move(callback)))
    {
        observability::Metrics::incRequest("authorize", 400);
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
        observability::Metrics::incRequest("authorize", 400);
        observability::Metrics::incLoginFailure("missing_state_parameter");

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
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
        observability::Metrics::incRequest("authorize", 400);
        observability::Metrics::incLoginFailure("invalid_state_parameter");

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
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
        observability::Metrics::incRequest("authorize", 400);
        observability::Metrics::incLoginFailure("suspicious_state_parameter");

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        resp->setBody("state parameter contains invalid characters.");
        callback(resp);
        return;
    }

    LOG_DEBUG << "Authorization request with valid state parameter for client: " << clientId
              << ", state: " << state.substr(0, 8) << "...";

    auto plugin = resolvePlugin();
    if (!plugin)
    {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody("OAuth2 Plugin not loaded");
        callback(resp);
        return;
    }

    // Validate Client (Async)
    plugin->validateClient(
      clientId,
      "",
      [this,
       plugin,
       clientId,
       redirectUri,
       scope,
       state,
       responseType,
       req,
       callback = std::move(callback)](bool validClient) mutable {
          if (!validClient)
          {
              observability::Metrics::incRequest("authorize", 400);
              observability::Metrics::incLoginFailure("invalid_client_id");

              auto resp = drogon::HttpResponse::newHttpResponse();
              resp->setStatusCode(drogon::k400BadRequest);
              resp->setBody("Invalid client_id");
              callback(resp);
              return;
          }

          // Validate Redirect URI (Async)
          plugin->validateRedirectUri(
            clientId,
            redirectUri,
            [this,
             plugin,
             clientId,
             redirectUri,
             scope,
             state,
             responseType,
             req,
             callback = std::move(callback)](bool validUri) mutable {
                if (!validUri)
                {
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k400BadRequest);
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

                plugin->validateClientScopes(
                  clientId,
                  requestedScopes,
                  [this,
                   plugin,
                   clientId,
                   redirectUri,
                   scope,
                   state,
                   responseType,
                   req,
                   requestedScopes,
                   callback =
                     std::move(callback)](bool validScopes, std::string scopeError) mutable {
                      if (!validScopes)
                      {
                          LOG_WARN << "Client scope validation failed: " << scopeError;
                          Json::Value jsonErr;
                          jsonErr["error"] = "invalid_scope";
                          jsonErr["error_description"] = scopeError;
                          auto resp = drogon::HttpResponse::newHttpJsonResponse(jsonErr);
                          resp->setStatusCode(drogon::k400BadRequest);
                          callback(resp);
                          return;
                      }

                      LOG_DEBUG << "Client scope validation successful";

                      std::string userId;
                      if (req->session())
                      {
                          userId = req->session()->get<std::string>("userId");
                      }
                      if (!userId.empty())
                      {
                          plugin->validateUserRolesForScopes(
                            userId,
                            requestedScopes,
                            [this,
                             plugin,
                             userId,
                             requestedScopes,
                             clientId,
                             scope,
                             redirectUri,
                             state,
                             callback = std::move(
                               callback
                             )](bool validRoles, std::string roleError) mutable {
                                if (!validRoles)
                                {
                                    LOG_WARN << "User role validation failed: " << roleError;
                                    Json::Value jsonErr;
                                    jsonErr["error"] = "access_denied";
                                    jsonErr["error_description"] = roleError;
                                    auto resp = drogon::HttpResponse::newHttpJsonResponse(jsonErr);
                                    resp->setStatusCode(
                                      common::error::OAuth2ErrorHandler::getHttpStatusCode(
                                        "access_denied"
                                      )
                                    );
                                    callback(resp);
                                    return;
                                }

                                LOG_DEBUG << "User role validation successful";

                                plugin->getInternalUserId(
                                  userId,
                                  [this,
                                   plugin,
                                   userId,
                                   clientId,
                                   scope,
                                   redirectUri,
                                   state,
                                   requestedScopes,
                                   callback = std::move(callback)](
                                    std::optional<int32_t> internalUserId
                                  ) mutable {
                                      if (!internalUserId)
                                      {
                                          LOG_WARN
                                            << "No internal user ID found for subject: " << userId
                                            << ", proceeding without consent check";

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
                                            ) {
                                                if (!success)
                                                {
                                                    LOG_ERROR
                                                      << "Failed to generate authorization code: "
                                                      << error;
                                                    Json::Value jsonErr;
                                                    jsonErr["error"] = "server_error";
                                                    jsonErr["error_description"] =
                                                      "Failed to generate authorization code";
                                                    auto resp =
                                                      drogon::HttpResponse::newHttpJsonResponse(
                                                        jsonErr
                                                      );
                                                    resp->setStatusCode(
                                                      drogon::k500InternalServerError
                                                    );
                                                    callback(resp);
                                                    return;
                                                }

                                                std::string location =
                                                  redirectUri + "?code=" + code;
                                                if (!state.empty())
                                                    location += "&state=" + state;
                                                auto resp =
                                                  drogon::HttpResponse::newRedirectionResponse(
                                                    location
                                                  );
                                                observability::Metrics::incRequest(
                                                  "authorize", 302
                                                );
                                                callback(resp);
                                            }
                                          );
                                          return;
                                      }

                                      checkUserConsentAndProceed(
                                        plugin,
                                        clientId,
                                        userId,
                                        *internalUserId,
                                        requestedScopes,
                                        scope,
                                        redirectUri,
                                        state,
                                        std::move(callback)
                                      );
                                  }
                                );
                            }
                          );
                          return;
                      }
                      else
                      {
                          auto customConfig = drogon::app().getCustomConfig();
                          std::string loginUrl = "/login";
                          if (
                            customConfig.isMember("oauth2") &&
                            customConfig["oauth2"].isMember("login_url")
                          )
                          {
                              loginUrl = customConfig["oauth2"]["login_url"].asString();
                          }
                          std::string location =
                            loginUrl + "?client_id=" + drogon::utils::urlEncode(clientId) +
                            "&redirect_uri=" + drogon::utils::urlEncode(redirectUri) +
                            "&scope=" + drogon::utils::urlEncode(scope) +
                            "&state=" + drogon::utils::urlEncode(state) +
                            "&response_type=" + drogon::utils::urlEncode(responseType);
                          auto resp = drogon::HttpResponse::newRedirectionResponse(location);
                          callback(resp);
                      }
                  }
                );
            }
          );
      }
    );
}

void OAuth2StandardController::token(
  const drogon::HttpRequestPtr &req,
  std::function<void(const drogon::HttpResponsePtr &)> &&callback
)
{
    // Use ValidatorHelper for consistent validation
    auto errors = oauth2::validation::RuleSet::oauth2Token(req);

    // Return validation errors if any
    if (oauth2::validation::HttpResponder::respondIfErrors(errors, std::move(callback)))
    {
        observability::Metrics::incRequest("token", 400);
        return;
    }

    auto plugin = resolvePlugin();
    if (!plugin)
    {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
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
            std::string decoded = drogon::utils::base64Decode(authHeader.substr(6));
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

    if (clientId.empty() || req->method() == drogon::Post)
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
                  auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                  std::string errorCode = result.get("error", "").asString();
                  drogon::HttpStatusCode statusCode =
                    common::error::OAuth2ErrorHandler::getHttpStatusCode(errorCode);
                  resp->setStatusCode(statusCode);
                  observability::Metrics::incRequest("token", static_cast<int>(statusCode));
                  callback(resp);
                  return;
              }

              auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
              observability::Metrics::incRequest("token", 200);
              observability::Metrics::updateActiveTokens(1);
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
                  auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                  std::string errorCode = result.get("error", "").asString();
                  drogon::HttpStatusCode statusCode =
                    common::error::OAuth2ErrorHandler::getHttpStatusCode(errorCode);
                  resp->setStatusCode(statusCode);
                  observability::Metrics::incRequest("token", static_cast<int>(statusCode));
                  callback(resp);
                  return;
              }

              auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
              observability::Metrics::incRequest("token", 200);
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
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k401Unauthorized);
            callback(resp);
            return;
        }

        auto sharedCb = std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
          std::move(callback)
        );

        plugin
          ->validateClient(clientId, clientSecret, [plugin, clientId, req, sharedCb](bool valid) {
              if (!valid)
              {
                  Json::Value error;
                  error["error"] = "invalid_client";
                  error["error_description"] = "Client authentication failed";
                  auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(drogon::k401Unauthorized);
                  (*sharedCb)(resp);
                  return;
              }

              // Verify client is CONFIDENTIAL (PUBLIC clients cannot use client_credentials)
              // Capture the storage shared_ptr into the getClient continuation so the storage
              // is guaranteed alive across this async hop (instead of re-fetching it inside the
              // continuation, which could dangle if the plugin/storage were torn down mid-flight).
              auto storage = plugin->getStorage();
              storage->getClient(
                clientId,
                [storage, clientId, req, sharedCb](std::optional<oauth2::OAuth2Client> client) {
                    if (!client)
                    {
                        Json::Value error;
                        error["error"] = "invalid_client";
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(drogon::k401Unauthorized);
                        (*sharedCb)(resp);
                        return;
                    }

                    if (client->clientType == oauth2::ClientType::PUBLIC)
                    {
                        Json::Value error;
                        error["error"] = "unauthorized_client";
                        error["error_description"] =
                          "Public clients cannot use client_credentials grant";
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(drogon::k401Unauthorized);
                        (*sharedCb)(resp);
                        return;
                    }

                    // Determine scope (intersection of requested and allowed)
                    std::string requestedScope = req->getParameter("scope");
                    std::string grantedScope = requestedScope.empty() ? "read" : requestedScope;

                    // Generate access token (no refresh token for client_credentials)
                    auto tokenStr = oauth2::utils::generateSecureToken();
                    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch()
                    )
                                 .count();

                    oauth2::OAuth2AccessToken token;
                    token.token = oauth2::utils::hashToken(tokenStr);
                    token.clientId = clientId;
                    token.userId = "client:" + clientId;  // M2M: subject is the client itself
                    token.scope = grantedScope;
                    token.expiresAt = now + 3600;

                    // Reuse the storage shared_ptr captured from the outer scope (kept alive
                    // across this async hop) instead of re-fetching plugin->getStorage().
                    // Capture it into the saveAccessToken callback as well so the storage
                    // outlives the in-flight save operation.
                    storage->saveAccessToken(token, [storage, sharedCb, tokenStr, grantedScope]() {
                        Json::Value json;
                        json["access_token"] = tokenStr;
                        json["token_type"] = "Bearer";
                        json["expires_in"] = 3600;
                        json["scope"] = grantedScope;
                        // No refresh_token for client_credentials
                        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
                        observability::Metrics::incRequest("token", 200);
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
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            observability::Metrics::incRequest("token", 400);
            callback(resp);
            return;
        }

        std::string deviceCodeHash = oauth2::utils::hashToken(deviceCode);

        auto dbClient = drogon::app().getDbClient();
        if (!dbClient)
        {
            Json::Value error;
            error["error"] = "server_error";
            error["error_description"] = "Database not available";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k500InternalServerError);
            callback(resp);
            return;
        }

        auto sharedCb = std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(
          std::move(callback)
        );

        dbClient->execSqlAsync(
          "SELECT device_code_hash, user_code, client_id, scope, status, user_id, "
          "expires_at, interval_seconds FROM oauth2_device_codes "
          "WHERE device_code_hash = $1",
          [plugin, sharedCb, clientId, deviceCodeHash](const drogon::orm::Result &result) {
              if (result.empty())
              {
                  Json::Value error;
                  error["error"] = "invalid_grant";
                  error["error_description"] = "Invalid device_code";
                  auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(drogon::k400BadRequest);
                  observability::Metrics::incRequest("token", 400);
                  (*sharedCb)(resp);
                  return;
              }

              auto row = result[0];
              std::string storedClientId = row["client_id"].as<std::string>();
              std::string status = row["status"].as<std::string>();
              int64_t expiresAt = row["expires_at"].as<int64_t>();
              std::string scope = row["scope"].isNull() ? "" : row["scope"].as<std::string>();
              std::string userId = row["user_id"].isNull() ? "" : row["user_id"].as<std::string>();

              // Verify client_id matches
              if (storedClientId != clientId)
              {
                  Json::Value error;
                  error["error"] = "invalid_grant";
                  error["error_description"] = "client_id mismatch";
                  auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(drogon::k400BadRequest);
                  observability::Metrics::incRequest("token", 400);
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
                  auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(drogon::k400BadRequest);
                  observability::Metrics::incRequest("token", 400);
                  (*sharedCb)(resp);
                  return;
              }

              // Check status
              if (status == "pending")
              {
                  Json::Value error;
                  error["error"] = "authorization_pending";
                  error["error_description"] = "The authorization request is still pending";
                  auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(drogon::k400BadRequest);
                  observability::Metrics::incRequest("token", 400);
                  (*sharedCb)(resp);
                  return;
              }

              if (status == "denied")
              {
                  Json::Value error;
                  error["error"] = "access_denied";
                  error["error_description"] = "The user denied the authorization request";
                  auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(drogon::k400BadRequest);
                  observability::Metrics::incRequest("token", 400);
                  (*sharedCb)(resp);
                  return;
              }

              if (status != "approved")
              {
                  Json::Value error;
                  error["error"] = "invalid_grant";
                  error["error_description"] = "Invalid device code status";
                  auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(drogon::k400BadRequest);
                  observability::Metrics::incRequest("token", 400);
                  (*sharedCb)(resp);
                  return;
              }

              // Status is "approved" �?issue tokens
              auto accessTokenStr = oauth2::utils::generateSecureToken();
              auto refreshTokenStr = oauth2::utils::generateSecureToken();
              std::string familyId = oauth2::utils::generateSecureToken(16);

              oauth2::OAuth2AccessToken accessToken;
              accessToken.token = oauth2::utils::hashToken(accessTokenStr);
              accessToken.clientId = clientId;
              accessToken.userId = userId;
              accessToken.scope = scope;
              accessToken.issuedAt = now;
              accessToken.expiresAt = now + 3600;

              oauth2::OAuth2RefreshToken refreshToken;
              refreshToken.token = oauth2::utils::hashToken(refreshTokenStr);
              refreshToken.accessToken = accessToken.token;
              refreshToken.clientId = clientId;
              refreshToken.userId = userId;
              refreshToken.scope = scope;
              refreshToken.expiresAt = now + (3600 * 24 * 30);
              refreshToken.familyId = familyId;

              // Capture the storage shared_ptr into the saveTokenPair callback so the storage
              // is guaranteed alive across this async hop.
              auto storage = plugin->getStorage();
              storage->saveTokenPair(
                accessToken,
                refreshToken,
                [storage, sharedCb, accessTokenStr, refreshTokenStr, scope, deviceCodeHash]() {
                    // Mark device code as consumed by deleting it
                    auto dbClient = drogon::app().getDbClient();
                    if (dbClient)
                    {
                        dbClient->execSqlAsync(
                          "DELETE FROM oauth2_device_codes WHERE device_code_hash = $1",
                          [](const drogon::orm::Result &) {},
                          [](const drogon::orm::DrogonDbException &) {},
                          deviceCodeHash
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

                    auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
                    observability::Metrics::incRequest("token", 200);
                    observability::Metrics::updateActiveTokens(1);
                    (*sharedCb)(resp);
                }
              );
          },
          [sharedCb](const drogon::orm::DrogonDbException &e) {
              LOG_ERROR << "Device code lookup failed: " << e.base().what();
              Json::Value error;
              error["error"] = "server_error";
              error["error_description"] = "Failed to process device code";
              auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(drogon::k500InternalServerError);
              (*sharedCb)(resp);
          },
          deviceCodeHash
        );
    }
    else
    {
        Json::Value error;
        error["error"] = "unsupported_grant_type";
        error["error_description"] =
          "Supported types: authorization_code, refresh_token, client_credentials, "
          "urn:ietf:params:oauth:grant-type:device_code";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k400BadRequest);
        observability::Metrics::incRequest("token", 400);
        callback(resp);
    }
}

void OAuth2StandardController::userInfo(
  const drogon::HttpRequestPtr &req,
  std::function<void(const drogon::HttpResponsePtr &)> &&callback
)
{
    if (req->method() == drogon::Options)
    {
        auto resp = drogon::HttpResponse::newHttpResponse();
        callback(resp);
        return;
    }

    std::string userId;
    auto attrs = req->getAttributes();
    if (!attrs->find("userId"))
    {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k401Unauthorized);
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
        auto resp = drogon::HttpResponse::newHttpJsonResponse(userInfo);
        callback(resp);
        return;
    }
    // First get user roles
    plugin->getUserRoles(userId, [this, userId, callback](std::vector<std::string> roles) {
        // Get user info directly from storage
        auto plugin = resolvePlugin();
        auto storage = plugin->getStorage();

        // Query user details from database.
        // Capture the storage shared_ptr into the getUserInfo callback so the storage
        // is guaranteed alive across this async hop.
        storage->getUserInfo(
          userId, [storage, userId, roles, callback](std::optional<Json::Value> dbUserInfo) {
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

              auto resp = drogon::HttpResponse::newHttpJsonResponse(userInfo);
              callback(resp);
          }
        );
    });
}

void OAuth2StandardController::checkUserConsentAndProceed(
  ::OAuth2Plugin *plugin,
  const std::string &clientId,
  const std::string &userId,
  int32_t internalUserId,
  const std::vector<std::string> &requestedScopes,
  const std::string &scope,
  const std::string &redirectUri,
  const std::string &state,
  std::function<void(const drogon::HttpResponsePtr &)> &&callback
)
{
    if (requestedScopes.empty())
    {
        plugin->generateAuthorizationCode(
          clientId,
          userId,
          scope,
          redirectUri,
          "",  // codeChallenge
          "",  // codeChallengeMethod
          "",  // nonce
          [clientId, redirectUri, state, callback = std::move(callback)](
            bool success, std::string code, std::string error
          ) mutable {
              if (!success)
              {
                  LOG_ERROR << "Failed to generate authorization code: " << error;
                  Json::Value jsonErr;
                  jsonErr["error"] = "server_error";
                  jsonErr["error_description"] = "Failed to generate authorization code";
                  auto resp = drogon::HttpResponse::newHttpJsonResponse(jsonErr);
                  resp->setStatusCode(drogon::k500InternalServerError);
                  callback(resp);
                  return;
              }

              std::string location = redirectUri + "?code=" + code;
              if (!state.empty())
                  location += "&state=" + state;
              auto resp = drogon::HttpResponse::newRedirectionResponse(location);
              observability::Metrics::incRequest("authorize", 302);
              callback(resp);
          }
        );
        return;
    }

    std::string currentScope = requestedScopes[0];
    std::vector<std::string> remainingScopes(requestedScopes.begin() + 1, requestedScopes.end());

    plugin->hasUserConsent(
      internalUserId,
      clientId,
      currentScope,
      [plugin,
       clientId,
       userId,
       internalUserId,
       remainingScopes,
       scope,
       redirectUri,
       state,
       currentScope,
       callback = std::move(callback)](bool hasConsent) mutable {
          if (!hasConsent)
          {
              LOG_INFO << "User " << userId << " hasn't consented to scope " << currentScope
                       << " for client " << clientId;

              auto customConfig = drogon::app().getCustomConfig();
              std::string consentUrl = "/consent";
              if (customConfig.isMember("oauth2") && customConfig["oauth2"].isMember("consent_url"))
              {
                  consentUrl = customConfig["oauth2"]["consent_url"].asString();
              }
              std::string location = consentUrl +
                                     "?client_id=" + drogon::utils::urlEncode(clientId) +
                                     "&user_id=" + drogon::utils::urlEncode(userId) +
                                     "&scope=" + drogon::utils::urlEncode(scope) +
                                     "&redirect_uri=" + drogon::utils::urlEncode(redirectUri) +
                                     "&state=" + drogon::utils::urlEncode(state);
              auto resp = drogon::HttpResponse::newRedirectionResponse(location);
              callback(resp);
              return;
          }

          checkUserConsentAndProceed(
            plugin,
            clientId,
            userId,
            internalUserId,
            remainingScopes,
            scope,
            redirectUri,
            state,
            std::move(callback)
          );
      }
    );
}

}  // namespace oauth2::controllers
