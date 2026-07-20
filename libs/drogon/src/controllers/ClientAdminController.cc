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
struct ClientAdminControllerDocs
{
    ClientAdminControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo listClients;
        listClients.path = "/api/admin/clients";
        listClients.method = "GET";
        listClients.summary = "List OAuth2 Clients";
        listClients.description = "Get a paginated list of registered OAuth2 clients.";
        listClients.tags = {"Admin", "Clients"};
        listClients.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(listClients);

        ::authforge::drogon::observability::openapi::EndpointInfo createClient;
        createClient.path = "/api/admin/clients";
        createClient.method = "POST";
        createClient.summary = "Create OAuth2 Client";
        createClient.description = "Register a new OAuth2 client.";
        createClient.tags = {"Admin", "Clients"};
        createClient.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(createClient);

        ::authforge::drogon::observability::openapi::EndpointInfo getClient;
        getClient.path = "/api/admin/clients/{clientId}";
        getClient.method = "GET";
        getClient.summary = "Get Client Details";
        getClient.description = "Get details of a specific OAuth2 client by ID.";
        getClient.tags = {"Admin", "Clients"};
        getClient.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(getClient);

        ::authforge::drogon::observability::openapi::EndpointInfo updateClient;
        updateClient.path = "/api/admin/clients/{clientId}";
        updateClient.method = "PUT";
        updateClient.summary = "Update OAuth2 Client";
        updateClient.description = "Update details of a specific OAuth2 client.";
        updateClient.tags = {"Admin", "Clients"};
        updateClient.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(updateClient);

        ::authforge::drogon::observability::openapi::EndpointInfo deleteClient;
        deleteClient.path = "/api/admin/clients/{clientId}";
        deleteClient.method = "DELETE";
        deleteClient.summary = "Delete OAuth2 Client";
        deleteClient.description = "Delete a specific OAuth2 client.";
        deleteClient.tags = {"Admin", "Clients"};
        deleteClient.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(deleteClient);

        ::authforge::drogon::observability::openapi::EndpointInfo resetClientSecret;
        resetClientSecret.path = "/api/admin/clients/{clientId}/reset-secret";
        resetClientSecret.method = "POST";
        resetClientSecret.summary = "Reset Client Secret";
        resetClientSecret.description = "Reset the secret of a specific OAuth2 client.";
        resetClientSecret.tags = {"Admin", "Clients"};
        resetClientSecret.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(
          resetClientSecret
        );

        ::authforge::drogon::observability::openapi::EndpointInfo getClientScopes;
        getClientScopes.path = "/api/admin/clients/{clientId}/scopes";
        getClientScopes.method = "GET";
        getClientScopes.summary = "Get Client Scopes";
        getClientScopes.description = "Get the assigned scopes for an OAuth2 client.";
        getClientScopes.tags = {"Admin", "Clients"};
        getClientScopes.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(getClientScopes);

        ::authforge::drogon::observability::openapi::EndpointInfo updateClientScopes;
        updateClientScopes.path = "/api/admin/clients/{clientId}/scopes";
        updateClientScopes.method = "PUT";
        updateClientScopes.summary = "Update Client Scopes";
        updateClientScopes.description = "Update the assigned scopes for an OAuth2 client.";
        updateClientScopes.tags = {"Admin", "Clients"};
        updateClientScopes.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(
          updateClientScopes
        );
    }
} g_clientAdminControllerDocs;
}  // namespace

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
