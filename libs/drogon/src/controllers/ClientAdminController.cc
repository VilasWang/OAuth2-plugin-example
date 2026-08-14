#include <authforge/drogon/controllers/ClientAdminController.h>
#include <authforge/drogon/admin/ClientManagementService.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>

// M5 Task 29b (authforge-sdk-refactor): the inline raw-SQL DB access from the
// Task 29a verbatim move is now delegated to ClientManagementService (Mapper +
// Criteria, per .claude/rules/db-operations.md). This controller is now a thin
// HTTP adapter: parse request -> dispatch to service -> the service renders the
// final HttpResponse (success JSON or error envelope). Behavior is byte-for-byte
// equivalent to the pre-29b version (Admin API tests must stay green).

namespace authforge::drogon::controllers
{

namespace
{
namespace openapi = ::authforge::drogon::observability::openapi;

// #43 resource-scope authorization: declare one EndpointInfo with its
// requiredScopes + impliedBy. All client-admin routes are admin-gated; the
// `admin` super-scope (in impliedBy) satisfies any of them.
openapi::EndpointInfo adminEp(
  const char *path,
  const char *method,
  const char *summary,
  const char *description,
  std::vector<std::string> requiredScopes)
{
    openapi::EndpointInfo ep;
    ep.path = path;
    ep.method = method;
    ep.summary = summary;
    ep.description = description;
    ep.tags = {"Admin", "Clients"};
    ep.requiresAuth = true;
    ep.requiredScopes = std::move(requiredScopes);
    ep.impliedBy = {"admin"};
    return ep;
}
}  // namespace

void ClientAdminController::initApiDocs()
{
    static std::once_flag docsOnce;
    std::call_once(docsOnce, [] { initApiDocsImpl(); });
}

void ClientAdminController::initApiDocsImpl()
{
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/clients", "GET", "List OAuth2 Clients",
              "Get a paginated list of registered OAuth2 clients.", {"clients:read"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/clients", "POST", "Create OAuth2 Client",
              "Register a new OAuth2 client.", {"clients:write"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/clients/{clientId}", "GET", "Get Client Details",
              "Get details of a specific OAuth2 client by ID.", {"clients:read"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/clients/{clientId}", "PUT", "Update OAuth2 Client",
              "Update details of a specific OAuth2 client.", {"clients:write"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/clients/{clientId}", "DELETE", "Delete OAuth2 Client",
              "Delete a specific OAuth2 client.", {"clients:write"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/clients/{clientId}/reset-secret", "POST", "Reset Client Secret",
              "Reset the secret of a specific OAuth2 client.", {"clients:write"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/clients/{clientId}/scopes", "GET", "Get Client Scopes",
              "Get the assigned scopes for an OAuth2 client.", {"clients:read"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/clients/{clientId}/scopes", "PUT", "Update Client Scopes",
              "Update the assigned scopes for an OAuth2 client.", {"clients:write"}));
}

using ClientService = ::authforge::drogon::admin::ClientManagementService;

void ClientAdminController::listClients(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    ClientService::listClients(req, sharedCb);
}

void ClientAdminController::createClient(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    ClientService::createClient(req, sharedCb);
}

void ClientAdminController::getClient(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    ClientService::getClient(req, sharedCb, clientId);
}

void ClientAdminController::updateClient(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    ClientService::updateClient(req, sharedCb, clientId);
}

void ClientAdminController::deleteClient(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    ClientService::deleteClient(req, sharedCb, clientId);
}

void ClientAdminController::resetClientSecret(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    ClientService::resetClientSecret(req, sharedCb, clientId);
}

void ClientAdminController::getClientScopes(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    ClientService::getClientScopes(req, sharedCb, clientId);
}

void ClientAdminController::updateClientScopes(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    ClientService::updateClientScopes(req, sharedCb, clientId);
}

}  // namespace authforge::drogon::controllers
