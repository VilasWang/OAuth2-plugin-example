#include <authforge/drogon/controllers/TokenAdminController.h>
#include <authforge/drogon/admin/TokenManagementService.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>

#include <memory>

// M5 Task 29b batch 2 (authforge-sdk-refactor): inline raw-SQL DB access from
// the Task 29a verbatim move is now delegated to TokenManagementService (Mapper
// + Criteria, per .claude/rules/db-operations.md). This controller is now a
// thin HTTP adapter. Behavior is byte-for-byte equivalent (Admin API tests must
// stay green).

namespace authforge::drogon::controllers
{

namespace
{
struct TokenAdminControllerDocs
{
    TokenAdminControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo listTokens;
        listTokens.path = "/api/admin/tokens";
        listTokens.method = "GET";
        listTokens.summary = "List Tokens";
        listTokens.description = "Get a list of active OAuth2 tokens.";
        listTokens.tags = {"Admin", "Tokens"};
        listTokens.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(listTokens);

        ::authforge::drogon::observability::openapi::EndpointInfo revokeTokensByClient;
        revokeTokensByClient.path = "/api/admin/tokens/revoke-by-client";
        revokeTokensByClient.method = "POST";
        revokeTokensByClient.summary = "Revoke Tokens By Client";
        revokeTokensByClient.description = "Revoke all tokens issued to a specific client.";
        revokeTokensByClient.tags = {"Admin", "Tokens"};
        revokeTokensByClient.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(
          revokeTokensByClient
        );

        ::authforge::drogon::observability::openapi::EndpointInfo revokeTokensByUser;
        revokeTokensByUser.path = "/api/admin/tokens/revoke-by-user";
        revokeTokensByUser.method = "POST";
        revokeTokensByUser.summary = "Revoke Tokens By User";
        revokeTokensByUser.description = "Revoke all tokens issued for a specific user.";
        revokeTokensByUser.tags = {"Admin", "Tokens"};
        revokeTokensByUser.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(
          revokeTokensByUser
        );

        ::authforge::drogon::observability::openapi::EndpointInfo revokeToken;
        revokeToken.path = "/api/admin/tokens/{tokenPrefix}";
        revokeToken.method = "DELETE";
        revokeToken.summary = "Revoke Token";
        revokeToken.description = "Revoke a specific token by its prefix.";
        revokeToken.tags = {"Admin", "Tokens"};
        revokeToken.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(revokeToken);

        ::authforge::drogon::observability::openapi::EndpointInfo getOidcKeys;
        getOidcKeys.path = "/api/admin/oidc/keys";
        getOidcKeys.method = "GET";
        getOidcKeys.summary = "Get OIDC Keys Info";
        getOidcKeys.description = "Get information about OIDC signing keys.";
        getOidcKeys.tags = {"Admin", "OIDC"};
        getOidcKeys.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(getOidcKeys);
    }
} g_tokenAdminControllerDocs;
}  // namespace

using TokenService = ::authforge::drogon::admin::TokenManagementService;

void TokenAdminController::listTokens(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    TokenService::listTokens(req, sharedCb);
}

void TokenAdminController::revokeToken(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &tokenPrefix
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    TokenService::revokeToken(req, sharedCb, tokenPrefix);
}

void TokenAdminController::revokeTokensByClient(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    TokenService::revokeTokensByClient(req, sharedCb);
}

void TokenAdminController::revokeTokensByUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    TokenService::revokeTokensByUser(req, sharedCb);
}

void TokenAdminController::getOidcKeys(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    (void)req;  // no DB access, no auth-derived behavior in this metadata route
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    TokenService::getOidcKeys(sharedCb);
}

}  // namespace authforge::drogon::controllers
