#include <fulla/drogon/controllers/TokenAdminController.h>
#include <fulla/drogon/admin/TokenManagementService.h>
#include <fulla/drogon/observability/openapi/OpenApiGenerator.h>

#include <memory>

// M5 Task 29b batch 2 (fulla-sdk-refactor): inline raw-SQL DB access from
// the Task 29a verbatim move is now delegated to TokenManagementService (Mapper
// + Criteria, per .claude/rules/db-operations.md). This controller is now a
// thin HTTP adapter. Behavior is byte-for-byte equivalent (Admin API tests must
// stay green).

namespace fulla::drogon::controllers
{

namespace
{
namespace openapi = ::fulla::drogon::observability::openapi;

// #43 resource-scope authorization: declare one EndpointInfo with its
// requiredScopes + impliedBy. All token-admin routes are admin-gated; the
// `admin` super-scope (in impliedBy) satisfies any of them. `tags` is a
// parameter because this controller mixes the Tokens and OIDC tag groups.
openapi::EndpointInfo adminEp(
  const char *path,
  const char *method,
  const char *summary,
  const char *description,
  std::vector<std::string> tags,
  std::vector<std::string> requiredScopes)
{
    openapi::EndpointInfo ep;
    ep.path = path;
    ep.method = method;
    ep.summary = summary;
    ep.description = description;
    ep.tags = std::move(tags);
    ep.requiresAuth = true;
    ep.requiredScopes = std::move(requiredScopes);
    ep.impliedBy = {"admin"};
    return ep;
}
}  // namespace

void TokenAdminController::initApiDocs()
{
    static std::once_flag docsOnce;
    std::call_once(docsOnce, [] { initApiDocsImpl(); });
}

void TokenAdminController::initApiDocsImpl()
{
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/tokens", "GET", "List Tokens",
              "Get a list of active OAuth2 tokens.", {"Admin", "Tokens"}, {"tokens:read"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/tokens/revoke-by-client", "POST", "Revoke Tokens By Client",
              "Revoke all tokens issued to a specific client.", {"Admin", "Tokens"},
              {"tokens:write"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/tokens/revoke-by-user", "POST", "Revoke Tokens By User",
              "Revoke all tokens issued for a specific user.", {"Admin", "Tokens"},
              {"tokens:write"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/tokens/{tokenPrefix}", "DELETE", "Revoke Token",
              "Revoke a specific token by its prefix.", {"Admin", "Tokens"}, {"tokens:write"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/oidc/keys", "GET", "Get OIDC Keys Info",
              "Get information about OIDC signing keys.", {"Admin", "OIDC"}, {"audit:read"}));
}

using TokenService = ::fulla::drogon::admin::TokenManagementService;

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

}  // namespace fulla::drogon::controllers
