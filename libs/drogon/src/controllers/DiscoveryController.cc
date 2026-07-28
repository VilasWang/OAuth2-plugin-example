#include <authforge/drogon/controllers/DiscoveryController.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <authforge/drogon/error/OAuth2ErrorHandler.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <drogon/drogon.h>
#include <mutex>

using namespace authforge::drogon::controllers;
using namespace authforge::drogon::observability::openapi;

namespace authforge::drogon::controllers
{

::OAuth2Plugin *DiscoveryController::resolvePlugin() const
{
    return plugin_ ? plugin_ : ::drogon::app().getPlugin<::OAuth2Plugin>();
}

void DiscoveryController::initApiDocs()
{
    static std::once_flag docsOnce;
    std::call_once(docsOnce, [] { initApiDocsImpl(); });
}

void DiscoveryController::initApiDocsImpl()
{
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

void DiscoveryController::metadata(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    LOG_DEBUG << "Metadata endpoint requested";

    // Get OAuth2 plugin
    auto plugin = resolvePlugin();
    if (!plugin)
    {
        authforge::common::error::OAuth2ErrorHandler::sendErrorResponse(
          std::move(callback), "server_error", "OAuth2 plugin not available"
        );
        return;
    }

    // Base URL from configuration (required for production)
    std::string baseUrl;
    auto customConfig = ::drogon::app().getCustomConfig();
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

    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(metadata);
    resp->setContentTypeCode(::drogon::CT_APPLICATION_JSON);
    callback(resp);
}

void DiscoveryController::oidcDiscovery(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    // OpenID Connect Discovery 1.0
    std::string baseUrl;
    auto customConfig = ::drogon::app().getCustomConfig();
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

    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(discovery);
    resp->setContentTypeCode(::drogon::CT_APPLICATION_JSON);
    callback(resp);
}

void DiscoveryController::jwks(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto plugin = resolvePlugin();
    if (!plugin || !plugin->getJwkManager())
    {
        Json::Value empty;
        empty["keys"] = Json::Value(Json::arrayValue);
        auto resp = ::drogon::HttpResponse::newHttpJsonResponse(empty);
        callback(resp);
        return;
    }

    auto jwks = plugin->getJwkManager()->getJwks();
    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(jwks);
    resp->setContentTypeCode(::drogon::CT_APPLICATION_JSON);
    // Cache JWKS for 1 hour (keys don't change frequently)
    resp->addHeader("Cache-Control", "public, max-age=3600");
    callback(resp);
}

}  // namespace authforge::drogon::controllers
