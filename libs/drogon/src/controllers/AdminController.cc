#include <authforge/drogon/controllers/AdminController.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <oauth2/utils/CryptoUtils.h>
#include <oauth2/utils/EmailNormalizer.h>
#include <oauth2/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <atomic>
#include <mutex>

namespace authforge::drogon::controllers
{

namespace
{
// Emit an Application error via the unified ErrorResponder entry point so the
// body is always an Error Envelope (Requirement 7.1 / 7.3 / 7.5).
void respondError(
  const ::drogon::HttpRequestPtr &req,
  const std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>> &cb,
  std::string code,
  std::string detailForLog = ""
)
{
    ::common::error::ErrorResponder::respond(
      req, [cb](const ::drogon::HttpResponsePtr &r) { (*cb)(r); }, std::move(code), std::move(detailForLog)
    );
}

struct AdminApiControllerDocs
{
    AdminApiControllerDocs()
    {
        ::oauth2::observability::openapi::EndpointInfo listClients;
        listClients.path = "/api/admin/clients";
        listClients.method = "GET";
        listClients.summary = "List OAuth2 Clients";
        listClients.description = "Get a paginated list of registered OAuth2 clients.";
        listClients.tags = {"Admin", "Clients"};
        listClients.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(listClients);

        ::oauth2::observability::openapi::EndpointInfo createClient;
        createClient.path = "/api/admin/clients";
        createClient.method = "POST";
        createClient.summary = "Create OAuth2 Client";
        createClient.description = "Register a new OAuth2 client.";
        createClient.tags = {"Admin", "Clients"};
        createClient.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(createClient);

        ::oauth2::observability::openapi::EndpointInfo getClient;
        getClient.path = "/api/admin/clients/{clientId}";
        getClient.method = "GET";
        getClient.summary = "Get Client Details";
        getClient.description = "Get details of a specific OAuth2 client by ID.";
        getClient.tags = {"Admin", "Clients"};
        getClient.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(getClient);

        ::oauth2::observability::openapi::EndpointInfo updateClient;
        updateClient.path = "/api/admin/clients/{clientId}";
        updateClient.method = "PUT";
        updateClient.summary = "Update OAuth2 Client";
        updateClient.description = "Update details of a specific OAuth2 client.";
        updateClient.tags = {"Admin", "Clients"};
        updateClient.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(updateClient);

        ::oauth2::observability::openapi::EndpointInfo deleteClient;
        deleteClient.path = "/api/admin/clients/{clientId}";
        deleteClient.method = "DELETE";
        deleteClient.summary = "Delete OAuth2 Client";
        deleteClient.description = "Delete a specific OAuth2 client.";
        deleteClient.tags = {"Admin", "Clients"};
        deleteClient.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(deleteClient);

        ::oauth2::observability::openapi::EndpointInfo resetClientSecret;
        resetClientSecret.path = "/api/admin/clients/{clientId}/reset-secret";
        resetClientSecret.method = "POST";
        resetClientSecret.summary = "Reset Client Secret";
        resetClientSecret.description = "Reset the secret of a specific OAuth2 client.";
        resetClientSecret.tags = {"Admin", "Clients"};
        resetClientSecret.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(resetClientSecret);

        ::oauth2::observability::openapi::EndpointInfo getClientScopes;
        getClientScopes.path = "/api/admin/clients/{clientId}/scopes";
        getClientScopes.method = "GET";
        getClientScopes.summary = "Get Client Scopes";
        getClientScopes.description = "Get the assigned scopes for an OAuth2 client.";
        getClientScopes.tags = {"Admin", "Clients"};
        getClientScopes.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(getClientScopes);

        ::oauth2::observability::openapi::EndpointInfo updateClientScopes;
        updateClientScopes.path = "/api/admin/clients/{clientId}/scopes";
        updateClientScopes.method = "PUT";
        updateClientScopes.summary = "Update Client Scopes";
        updateClientScopes.description = "Update the assigned scopes for an OAuth2 client.";
        updateClientScopes.tags = {"Admin", "Clients"};
        updateClientScopes.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(updateClientScopes);

        ::oauth2::observability::openapi::EndpointInfo listUsers;
        listUsers.path = "/api/admin/users";
        listUsers.method = "GET";
        listUsers.summary = "List Users";
        listUsers.description = "Get a paginated list of users.";
        listUsers.tags = {"Admin", "Users"};
        listUsers.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(listUsers);

        ::oauth2::observability::openapi::EndpointInfo disableUser;
        disableUser.path = "/api/admin/users/{userId}/disable";
        disableUser.method = "PUT";
        disableUser.summary = "Disable User";
        disableUser.description = "Disable a specific user account.";
        disableUser.tags = {"Admin", "Users"};
        disableUser.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(disableUser);

        ::oauth2::observability::openapi::EndpointInfo assignUserRoles;
        assignUserRoles.path = "/api/admin/users/{userId}/roles";
        assignUserRoles.method = "PUT";
        assignUserRoles.summary = "Assign User Roles";
        assignUserRoles.description = "Assign roles to a specific user.";
        assignUserRoles.tags = {"Admin", "Users"};
        assignUserRoles.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(assignUserRoles);

        ::oauth2::observability::openapi::EndpointInfo listScopes;
        listScopes.path = "/api/admin/scopes";
        listScopes.method = "GET";
        listScopes.summary = "List Scopes";
        listScopes.description = "Get a list of all available scopes.";
        listScopes.tags = {"Admin", "Scopes"};
        listScopes.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(listScopes);

        ::oauth2::observability::openapi::EndpointInfo listLogs;
        listLogs.path = "/api/admin/logs";
        listLogs.method = "GET";
        listLogs.summary = "List Audit Logs";
        listLogs.description = "Get a paginated list of system audit logs.";
        listLogs.tags = {"Admin", "Logs"};
        listLogs.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(listLogs);

        ::oauth2::observability::openapi::EndpointInfo listTokens;
        listTokens.path = "/api/admin/tokens";
        listTokens.method = "GET";
        listTokens.summary = "List Tokens";
        listTokens.description = "Get a list of active OAuth2 tokens.";
        listTokens.tags = {"Admin", "Tokens"};
        listTokens.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(listTokens);

        ::oauth2::observability::openapi::EndpointInfo revokeTokensByClient;
        revokeTokensByClient.path = "/api/admin/tokens/revoke-by-client";
        revokeTokensByClient.method = "POST";
        revokeTokensByClient.summary = "Revoke Tokens By Client";
        revokeTokensByClient.description = "Revoke all tokens issued to a specific client.";
        revokeTokensByClient.tags = {"Admin", "Tokens"};
        revokeTokensByClient.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(revokeTokensByClient);

        ::oauth2::observability::openapi::EndpointInfo revokeTokensByUser;
        revokeTokensByUser.path = "/api/admin/tokens/revoke-by-user";
        revokeTokensByUser.method = "POST";
        revokeTokensByUser.summary = "Revoke Tokens By User";
        revokeTokensByUser.description = "Revoke all tokens issued for a specific user.";
        revokeTokensByUser.tags = {"Admin", "Tokens"};
        revokeTokensByUser.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(revokeTokensByUser);

        ::oauth2::observability::openapi::EndpointInfo revokeToken;
        revokeToken.path = "/api/admin/tokens/{tokenPrefix}";
        revokeToken.method = "DELETE";
        revokeToken.summary = "Revoke Token";
        revokeToken.description = "Revoke a specific token by its prefix.";
        revokeToken.tags = {"Admin", "Tokens"};
        revokeToken.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(revokeToken);

        ::oauth2::observability::openapi::EndpointInfo getOidcKeys;
        getOidcKeys.path = "/api/admin/oidc/keys";
        getOidcKeys.method = "GET";
        getOidcKeys.summary = "Get OIDC Keys Info";
        getOidcKeys.description = "Get information about OIDC signing keys.";
        getOidcKeys.tags = {"Admin", "OIDC"};
        getOidcKeys.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(getOidcKeys);

        // New endpoints
        ::oauth2::observability::openapi::EndpointInfo getUser;
        getUser.path = "/api/admin/users/{userId}";
        getUser.method = "GET";
        getUser.summary = "Get User Detail";
        getUser.description =
          "Get detailed information about a specific user including roles and account status.";
        getUser.tags = {"Admin", "Users"};
        getUser.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(getUser);

        ::oauth2::observability::openapi::EndpointInfo updateUser;
        updateUser.path = "/api/admin/users/{userId}";
        updateUser.method = "PUT";
        updateUser.summary = "Update User";
        updateUser.description = "Update user information (email, email_verified).";
        updateUser.tags = {"Admin", "Users"};
        updateUser.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(updateUser);

        ::oauth2::observability::openapi::EndpointInfo enableUser;
        enableUser.path = "/api/admin/users/{userId}/enable";
        enableUser.method = "POST";
        enableUser.summary = "Enable User";
        enableUser.description = "Enable a disabled user account by resetting lockout state.";
        enableUser.tags = {"Admin", "Users"};
        enableUser.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(enableUser);

        ::oauth2::observability::openapi::EndpointInfo getUserRoles;
        getUserRoles.path = "/api/admin/users/{userId}/roles";
        getUserRoles.method = "GET";
        getUserRoles.summary = "Get User Roles";
        getUserRoles.description = "Get the roles assigned to a specific user.";
        getUserRoles.tags = {"Admin", "Users"};
        getUserRoles.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(getUserRoles);

        ::oauth2::observability::openapi::EndpointInfo listRoles;
        listRoles.path = "/api/admin/roles";
        listRoles.method = "GET";
        listRoles.summary = "List Roles";
        listRoles.description = "Get a list of all roles with user counts.";
        listRoles.tags = {"Admin", "Roles"};
        listRoles.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(listRoles);

        ::oauth2::observability::openapi::EndpointInfo createRole;
        createRole.path = "/api/admin/roles";
        createRole.method = "POST";
        createRole.summary = "Create Role";
        createRole.description = "Create a new role. Built-in roles cannot be duplicated.";
        createRole.tags = {"Admin", "Roles"};
        createRole.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(createRole);

        ::oauth2::observability::openapi::EndpointInfo updateRole;
        updateRole.path = "/api/admin/roles/{roleId}";
        updateRole.method = "PUT";
        updateRole.summary = "Update Role";
        updateRole.description = "Update a role's description.";
        updateRole.tags = {"Admin", "Roles"};
        updateRole.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(updateRole);

        ::oauth2::observability::openapi::EndpointInfo deleteRole;
        deleteRole.path = "/api/admin/roles/{roleId}";
        deleteRole.method = "DELETE";
        deleteRole.summary = "Delete Role";
        deleteRole.description = "Delete a role. Built-in roles (admin, user) cannot be deleted.";
        deleteRole.tags = {"Admin", "Roles"};
        deleteRole.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(deleteRole);

        ::oauth2::observability::openapi::EndpointInfo createScope;
        createScope.path = "/api/admin/scopes";
        createScope.method = "POST";
        createScope.summary = "Create Scope";
        createScope.description = "Create a new OAuth2 scope.";
        createScope.tags = {"Admin", "Scopes"};
        createScope.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(createScope);

        ::oauth2::observability::openapi::EndpointInfo updateScope;
        updateScope.path = "/api/admin/scopes/{scopeId}";
        updateScope.method = "PUT";
        updateScope.summary = "Update Scope";
        updateScope.description = "Update a scope's properties.";
        updateScope.tags = {"Admin", "Scopes"};
        updateScope.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(updateScope);

        ::oauth2::observability::openapi::EndpointInfo deleteScope;
        deleteScope.path = "/api/admin/scopes/{scopeId}";
        deleteScope.method = "DELETE";
        deleteScope.summary = "Delete Scope";
        deleteScope.description = "Delete a scope. Built-in scopes cannot be deleted.";
        deleteScope.tags = {"Admin", "Scopes"};
        deleteScope.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(deleteScope);

        ::oauth2::observability::openapi::EndpointInfo getDashboardStats;
        getDashboardStats.path = "/api/admin/dashboard/stats";
        getDashboardStats.method = "GET";
        getDashboardStats.summary = "Get Dashboard Stats";
        getDashboardStats.description =
          "Get dashboard statistics including user count, client count, active tokens, and failure "
          "metrics.";
        getDashboardStats.tags = {"Admin", "Dashboard"};
        getDashboardStats.requiresAuth = true;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(getDashboardStats);
    }
};

AdminApiControllerDocs docs_;
}  // namespace

void AdminController::listClients(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT client_id, client_type, name, redirect_uris, allowed_grant_types "
          "FROM oauth2_clients ORDER BY client_id",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              Json::Value json;
              json["status"] = "success";
              Json::Value clients(Json::arrayValue);

              for (const auto &row : result)
              {
                  Json::Value client;
                  client["client_id"] = row["client_id"].as<std::string>();
                  client["client_type"] = row["client_type"].as<std::string>();
                  client["name"] = row["name"].isNull() ? "" : row["name"].as<std::string>();
                  client["redirect_uris"] =
                    row["redirect_uris"].isNull() ? "" : row["redirect_uris"].as<std::string>();
                  client["allowed_grant_types"] = row["allowed_grant_types"].isNull()
                                                    ? ""
                                                    : row["allowed_grant_types"].as<std::string>();
                  clients.append(client);
              }

              json["clients"] = clients;
              json["total"] = static_cast<int>(result.size());
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch clients: ") + e.base().what()
              );
          }
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::createClient(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    // Parse request body
    std::string name;
    std::string redirectUris;
    std::string allowedGrantTypes = "authorization_code";
    std::string clientType = "CONFIDENTIAL";

    auto jsonBody = req->getJsonObject();
    if (jsonBody)
    {
        name = jsonBody->get("name", "").asString();
        redirectUris = jsonBody->get("redirect_uris", "").asString();
        allowedGrantTypes = jsonBody->get("allowed_grant_types", "authorization_code").asString();
        clientType = jsonBody->get("client_type", "CONFIDENTIAL").asString();
    }

    // Generate client_id (UUID) and client_secret
    std::string clientId = ::drogon::utils::getUuid();
    std::string clientSecret = ::oauth2::utils::generateSecureToken();
    std::string secretHash = ::oauth2::utils::hashToken(clientSecret);
    std::string salt = ::drogon::utils::getUuid().substr(0, 36);

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "INSERT INTO oauth2_clients (client_id, client_type, client_secret, salt, name, "
          "redirect_uris, allowed_grant_types) VALUES ($1, $2, $3, $4, $5, $6, $7)",
          [sharedCb, req, clientId, clientSecret](const ::drogon::orm::Result &) {
              Json::Value json;
              json["status"] = "success";
              json["message"] = "Client created successfully";
              json["client_id"] = clientId;
              json["client_secret"] = clientSecret;  // Only returned once at creation time
              json["note"] = "Store the client_secret securely. It will not be shown again.";
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              resp->setStatusCode(::drogon::k201Created);
              (*sharedCb)(resp);
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to create client: ") + e.base().what()
              );
          },
          clientId,
          clientType,
          secretHash,
          salt,
          name,
          redirectUris,
          allowedGrantTypes
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::getClient(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (clientId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "clientId is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT client_id, client_type, name, redirect_uris, allowed_grant_types "
          "FROM oauth2_clients WHERE client_id = $1",
          [sharedCb, req, clientId, db](const ::drogon::orm::Result &result) {
              if (result.empty())
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "Client not found");
                  return;
              }

              const auto &row = result[0];
              Json::Value json;
              json["status"] = "success";
              json["client_id"] = row["client_id"].as<std::string>();
              json["client_type"] = row["client_type"].as<std::string>();
              json["name"] = row["name"].isNull() ? "" : row["name"].as<std::string>();
              json["redirect_uris"] =
                row["redirect_uris"].isNull() ? "" : row["redirect_uris"].as<std::string>();
              json["allowed_grant_types"] = row["allowed_grant_types"].isNull()
                                              ? ""
                                              : row["allowed_grant_types"].as<std::string>();
              // Note: oauth2_clients has no created_at column

              // Also fetch scopes for this client
              db->execSqlAsync(
                "SELECT scope_name FROM oauth2_client_scopes WHERE client_id = $1",
                [sharedCb, req, json](const ::drogon::orm::Result &scopeResult) mutable {
                    Json::Value scopes(Json::arrayValue);
                    for (const auto &scopeRow : scopeResult)
                    {
                        scopes.append(scopeRow["scope_name"].as<std::string>());
                    }
                    json["scopes"] = scopes;
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                },
                [sharedCb, req, json](const ::drogon::orm::DrogonDbException &) mutable {
                    // Return client info even if scope query fails
                    json["scopes"] = Json::Value(Json::arrayValue);
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                },
                clientId
              );
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch client: ") + e.base().what()
              );
          },
          clientId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::updateClient(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (clientId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "clientId is required");
        return;
    }

    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "Invalid JSON body");
        return;
    }

    // Build SET clause dynamically based on provided fields
    std::vector<std::string> setClauses;
    std::vector<std::string> params;
    int paramIdx = 1;

    if (jsonBody->isMember("name"))
    {
        setClauses.push_back("name = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["name"].asString());
    }
    if (jsonBody->isMember("redirect_uris"))
    {
        setClauses.push_back("redirect_uris = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["redirect_uris"].asString());
    }
    if (jsonBody->isMember("allowed_grant_types"))
    {
        setClauses.push_back("allowed_grant_types = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["allowed_grant_types"].asString());
    }

    if (setClauses.empty())
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "No fields to update");
        return;
    }

    std::string query = "UPDATE oauth2_clients SET ";
    for (size_t i = 0; i < setClauses.size(); ++i)
    {
        if (i > 0)
            query += ", ";
        query += setClauses[i];
    }
    query += " WHERE client_id = $" + std::to_string(paramIdx);
    params.push_back(clientId);

    try
    {
        auto db = ::drogon::app().getDbClient();

        // Execute update based on number of params
        if (params.size() == 2)
        {
            db->execSqlAsync(
              query,
              [sharedCb, req, clientId](const ::drogon::orm::Result &result) {
                  if (result.affectedRows() == 0)
                  {
                      respondError(
                        req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "Client not found"
                      );
                      return;
                  }
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "Client updated successfully";
                  json["client_id"] = clientId;
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to update client: ") + e.base().what()
                  );
              },
              params[0],
              params[1]
            );
        }
        else if (params.size() == 3)
        {
            db->execSqlAsync(
              query,
              [sharedCb, req, clientId](const ::drogon::orm::Result &result) {
                  if (result.affectedRows() == 0)
                  {
                      respondError(
                        req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "Client not found"
                      );
                      return;
                  }
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "Client updated successfully";
                  json["client_id"] = clientId;
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to update client: ") + e.base().what()
                  );
              },
              params[0],
              params[1],
              params[2]
            );
        }
        else if (params.size() == 4)
        {
            db->execSqlAsync(
              query,
              [sharedCb, req, clientId](const ::drogon::orm::Result &result) {
                  if (result.affectedRows() == 0)
                  {
                      respondError(
                        req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "Client not found"
                      );
                      return;
                  }
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "Client updated successfully";
                  json["client_id"] = clientId;
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to update client: ") + e.base().what()
                  );
              },
              params[0],
              params[1],
              params[2],
              params[3]
            );
        }
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::getClientScopes(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (clientId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "clientId is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT scope_name FROM oauth2_client_scopes WHERE client_id = $1",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              Json::Value json;
              json["status"] = "success";
              Json::Value scopes(Json::arrayValue);
              for (const auto &row : result)
              {
                  scopes.append(row["scope_name"].as<std::string>());
              }
              json["scopes"] = scopes;
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch client scopes: ") + e.base().what()
              );
          },
          clientId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::updateClientScopes(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (clientId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "clientId is required");
        return;
    }

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("scopes") || !(*jsonBody)["scopes"].isArray())
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "Request body must contain a 'scopes' array"
        );
        return;
    }

    std::vector<std::string> scopes;
    for (const auto &scope : (*jsonBody)["scopes"])
    {
        if (scope.isString())
        {
            scopes.push_back(scope.asString());
        }
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        auto transaction = db->newTransaction();

        // Step 1: Delete existing scopes for this client
        transaction->execSqlAsync(
          "DELETE FROM oauth2_client_scopes WHERE client_id = $1",
          [sharedCb, req, clientId, scopes, transaction](const ::drogon::orm::Result &) {
              if (scopes.empty())
              {
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "Scopes updated";
                  json["scopes"] = Json::Value(Json::arrayValue);
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                  return;
              }

              // Step 2: Insert new scopes
              auto remaining = std::make_shared<std::atomic<int>>(static_cast<int>(scopes.size()));
              auto insertedScopes = std::make_shared<std::vector<std::string>>();
              auto mu = std::make_shared<std::mutex>();

              for (const auto &scopeName : scopes)
              {
                  transaction->execSqlAsync(
                    "INSERT INTO oauth2_client_scopes (client_id, scope_name) VALUES ($1, $2)",
                    [sharedCb, req, scopeName, remaining, insertedScopes, mu, scopes](
                      const ::drogon::orm::Result &
                    ) {
                        {
                            std::lock_guard<std::mutex> lock(*mu);
                            insertedScopes->push_back(scopeName);
                        }

                        if (remaining->fetch_sub(1) == 1)
                        {
                            Json::Value json;
                            json["status"] = "success";
                            json["message"] = "Scopes updated";
                            Json::Value scopesJson(Json::arrayValue);
                            {
                                std::lock_guard<std::mutex> lock(*mu);
                                for (const auto &s : *insertedScopes)
                                    scopesJson.append(s);
                            }
                            json["scopes"] = scopesJson;
                            (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                        }
                    },
                    [sharedCb, req, remaining](const ::drogon::orm::DrogonDbException &e) {
                        if (remaining->fetch_sub(1) == 1)
                        {
                            respondError(
                              req,
                              sharedCb,
                              "DB_QUERY_ERROR",
                              std::string("Failed to assign some scopes: ") + e.base().what()
                            );
                        }
                    },
                    clientId,
                    scopeName
                  );
              }
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to clear existing scopes: ") + e.base().what()
              );
          },
          clientId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::listUsers(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT id, username, email, email_verified, mfa_enabled "
          "FROM users ORDER BY id",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              Json::Value json;
              json["status"] = "success";
              Json::Value users(Json::arrayValue);

              for (const auto &row : result)
              {
                  Json::Value user;
                  user["id"] = row["id"].as<int>();
                  user["username"] = row["username"].as<std::string>();
                  user["email"] = row["email"].isNull() ? "" : row["email"].as<std::string>();
                  user["email_verified"] =
                    row["email_verified"].isNull() ? false : row["email_verified"].as<bool>();
                  user["mfa_enabled"] =
                    row["mfa_enabled"].isNull() ? false : row["mfa_enabled"].as<bool>();
                  users.append(user);
              }

              json["users"] = users;
              json["total"] = static_cast<int>(result.size());
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch users: ") + e.base().what()
              );
          }
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::listScopes(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT id, name, description, mapped_role, is_default, requires_admin_role "
          "FROM oauth2_scopes ORDER BY id",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              Json::Value json;
              json["status"] = "success";
              Json::Value scopes(Json::arrayValue);

              for (const auto &row : result)
              {
                  Json::Value scope;
                  scope["id"] = row["id"].as<int>();
                  scope["name"] = row["name"].as<std::string>();
                  scope["description"] =
                    row["description"].isNull() ? "" : row["description"].as<std::string>();
                  scope["mapped_role"] =
                    row["mapped_role"].isNull() ? "" : row["mapped_role"].as<std::string>();
                  scope["is_default"] =
                    row["is_default"].isNull() ? false : row["is_default"].as<bool>();
                  scope["requires_admin_role"] = row["requires_admin_role"].isNull()
                                                   ? false
                                                   : row["requires_admin_role"].as<bool>();
                  scopes.append(scope);
              }

              json["scopes"] = scopes;
              json["total"] = static_cast<int>(result.size());
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch scopes: ") + e.base().what()
              );
          }
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::deleteClient(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (clientId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "clientId is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "DELETE FROM oauth2_clients WHERE client_id = $1",
          [sharedCb, req, clientId](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "Client not found");
                  return;
              }

              Json::Value json;
              json["status"] = "success";
              json["message"] = "Client deleted successfully";
              json["client_id"] = clientId;
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              (*sharedCb)(resp);
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to delete client: ") + e.base().what()
              );
          },
          clientId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::disableUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "userId is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "UPDATE users SET locked_until = 9999999999 WHERE id = $1",
          [sharedCb, req, userId](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found");
                  return;
              }

              Json::Value json;
              json["status"] = "success";
              json["message"] = "User disabled successfully";
              json["user_id"] = userId;
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              (*sharedCb)(resp);
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to disable user: ") + e.base().what()
              );
          },
          userId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::assignUserRoles(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "userId is required");
        return;
    }

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("roles") || !(*jsonBody)["roles"].isArray())
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "Request body must contain a 'roles' array"
        );
        return;
    }

    std::vector<std::string> roles;
    for (const auto &role : (*jsonBody)["roles"])
    {
        if (role.isString())
        {
            roles.push_back(role.asString());
        }
    }

    try
    {
        auto db = ::drogon::app().getDbClient();

        // Step 1: Delete existing roles for this user
        db->execSqlAsync(
          "DELETE FROM user_roles WHERE user_id = $1",
          [sharedCb, req, userId, roles, db](const ::drogon::orm::Result &) {
              if (roles.empty())
              {
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "User roles updated successfully";
                  json["user_id"] = userId;
                  json["roles"] = Json::Value(Json::arrayValue);
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                  (*sharedCb)(resp);
                  return;
              }

              // Step 2: Insert new roles
              auto remaining = std::make_shared<std::atomic<int>>(static_cast<int>(roles.size()));
              auto assignedRoles = std::make_shared<std::vector<std::string>>();
              auto mu = std::make_shared<std::mutex>();

              for (const auto &roleName : roles)
              {
                  db->execSqlAsync(
                    "INSERT INTO user_roles (user_id, role_id) "
                    "SELECT $1, id FROM roles WHERE name = $2",
                    [sharedCb, req, userId, roleName, remaining, assignedRoles, mu](
                      const ::drogon::orm::Result &result
                    ) {
                        if (result.affectedRows() > 0)
                        {
                            std::lock_guard<std::mutex> lock(*mu);
                            assignedRoles->push_back(roleName);
                        }

                        if (remaining->fetch_sub(1) == 1)
                        {
                            // All inserts completed
                            Json::Value json;
                            json["status"] = "success";
                            json["message"] = "User roles updated successfully";
                            json["user_id"] = userId;
                            Json::Value rolesJson(Json::arrayValue);
                            {
                                std::lock_guard<std::mutex> lock(*mu);
                                for (const auto &r : *assignedRoles)
                                    rolesJson.append(r);
                            }
                            json["roles"] = rolesJson;
                            auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                            (*sharedCb)(resp);
                        }
                    },
                    [sharedCb, req, remaining](const ::drogon::orm::DrogonDbException &e) {
                        if (remaining->fetch_sub(1) == 1)
                        {
                            respondError(
                              req,
                              sharedCb,
                              "DB_QUERY_ERROR",
                              std::string("Failed to assign some roles: ") + e.base().what()
                            );
                        }
                    },
                    userId,
                    roleName
                  );
              }
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to clear existing roles: ") + e.base().what()
              );
          },
          userId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::resetClientSecret(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (clientId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "clientId is required");
        return;
    }

    // Generate new secret
    std::string newSecret = ::oauth2::utils::generateSecureToken();
    std::string newSecretHash = ::oauth2::utils::hashToken(newSecret);

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "UPDATE oauth2_clients SET client_secret = $1 WHERE client_id = $2",
          [sharedCb, req, clientId, newSecret](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "Client not found");
                  return;
              }

              Json::Value json;
              json["status"] = "success";
              json["message"] = "Client secret reset successfully";
              json["client_id"] = clientId;
              json["client_secret"] = newSecret;
              json["note"] = "Store the new client_secret securely. It will not be shown again.";
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              (*sharedCb)(resp);
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to reset client secret: ") + e.base().what()
              );
          },
          newSecretHash,
          clientId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::listLogs(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    // Parse query params for filtering
    int page = 1;
    int perPage = 50;
    std::string action = req->getParameter("action");
    std::string outcome = req->getParameter("outcome");
    std::string actorId = req->getParameter("actor_id");

    try
    {
        page = std::stoi(req->getParameter("page"));
    }
    catch (...)
    {
    }
    try
    {
        perPage = std::stoi(req->getParameter("per_page"));
    }
    catch (...)
    {
    }
    if (perPage > 100)
        perPage = 100;
    if (perPage < 1)
        perPage = 50;
    if (page < 1)
        page = 1;
    int offset = (page - 1) * perPage;

    try
    {
        auto db = ::drogon::app().getDbClient();

        // Build the parameterized WHERE clause from the optional action / outcome
        // / actor_id filters. The previous implementation built this clause but
        // then discarded it, executing an unfiltered query (A-LOG-004).
        std::string whereClause = " WHERE 1=1";
        std::string actionParam = action;
        std::string outcomeParam = outcome;
        std::string actorParam = actorId;
        int paramIdx = 1;
        if (!actionParam.empty())
            whereClause += " AND action = $" + std::to_string(paramIdx++);
        if (!outcomeParam.empty())
            whereClause += " AND outcome = $" + std::to_string(paramIdx++);
        if (!actorParam.empty())
            whereClause += " AND actor_id = $" + std::to_string(paramIdx++);

        std::string dataQuery =
          "SELECT id, timestamp, actor_type, actor_id, action, "
          "target_type, target_id, outcome, ip "
          "FROM audit_logs" +
          whereClause + " ORDER BY timestamp DESC LIMIT " + std::to_string(perPage) + " OFFSET " +
          std::to_string(offset);

        // Build the JSON response from the query result. `total` reflects the
        // number of rows on this page (matches the pre-fix behavior; the logs
        // table is not expected to drive precise pagination counts here).
        auto buildLogs = [sharedCb, req, page, perPage](const ::drogon::orm::Result &result) {
            Json::Value json;
            json["status"] = "success";
            json["page"] = page;
            json["per_page"] = perPage;
            json["total"] = static_cast<int>(result.size());
            Json::Value logs(Json::arrayValue);

            for (const auto &row : result)
            {
                Json::Value log;
                log["id"] = row["id"].as<int64_t>();
                log["timestamp"] =
                  row["timestamp"].isNull() ? "" : row["timestamp"].as<std::string>();
                log["actor_type"] =
                  row["actor_type"].isNull() ? "" : row["actor_type"].as<std::string>();
                log["actor_id"] = row["actor_id"].isNull() ? "" : row["actor_id"].as<std::string>();
                log["action"] = row["action"].isNull() ? "" : row["action"].as<std::string>();
                log["target_type"] =
                  row["target_type"].isNull() ? "" : row["target_type"].as<std::string>();
                log["target_id"] =
                  row["target_id"].isNull() ? "" : row["target_id"].as<std::string>();
                log["outcome"] = row["outcome"].isNull() ? "" : row["outcome"].as<std::string>();
                log["ip"] = row["ip"].isNull() ? "" : row["ip"].as<std::string>();
                logs.append(log);
            }

            json["logs"] = logs;
            (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
        };

        auto onDbError = [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
            respondError(
              req,
              sharedCb,
              "DB_QUERY_ERROR",
              std::string("Failed to fetch audit logs: ") + e.base().what()
            );
        };

        // Drogon's execSqlAsync binds trailing variadic arguments positionally;
        // dispatch on the number of active filters so the right overload is used.
        int nParams = (!actionParam.empty() ? 1 : 0) + (!outcomeParam.empty() ? 1 : 0) +
                      (!actorParam.empty() ? 1 : 0);

        if (nParams == 0)
        {
            db->execSqlAsync(dataQuery, buildLogs, onDbError);
        }
        else if (nParams == 1)
        {
            const std::string &p1 = !actionParam.empty()
                                      ? actionParam
                                      : (!outcomeParam.empty() ? outcomeParam : actorParam);
            db->execSqlAsync(dataQuery, buildLogs, onDbError, p1);
        }
        else if (nParams == 2)
        {
            // Bind list in declaration order: action, outcome, actor_id.
            std::vector<std::string> binds;
            if (!actionParam.empty())
                binds.push_back(actionParam);
            if (!outcomeParam.empty())
                binds.push_back(outcomeParam);
            if (!actorParam.empty())
                binds.push_back(actorParam);
            db->execSqlAsync(dataQuery, buildLogs, onDbError, binds[0], binds[1]);
        }
        else
        {
            db->execSqlAsync(
              dataQuery, buildLogs, onDbError, actionParam, outcomeParam, actorParam
            );
        }
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::listTokens(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    int page = 1;
    int perPage = 50;
    std::string clientIdFilter = req->getParameter("client_id");
    std::string userIdFilter = req->getParameter("user_id");

    try
    {
        page = std::stoi(req->getParameter("page"));
    }
    catch (...)
    {
    }
    try
    {
        perPage = std::stoi(req->getParameter("per_page"));
    }
    catch (...)
    {
    }
    if (perPage > 100)
        perPage = 100;
    if (perPage < 1)
        perPage = 50;
    if (page < 1)
        page = 1;
    int offset = (page - 1) * perPage;

    try
    {
        auto db = ::drogon::app().getDbClient();

        // Build count query and data query with filters
        // expires_at is stored as BIGINT (Unix epoch seconds)
        std::string whereClause =
          " WHERE expires_at > EXTRACT(EPOCH FROM NOW())::BIGINT AND (revoked IS NULL OR revoked = "
          "FALSE)";
        std::string filterParams;
        int paramIdx = 1;

        if (!clientIdFilter.empty())
        {
            whereClause += " AND client_id = $" + std::to_string(paramIdx++);
        }
        if (!userIdFilter.empty())
        {
            whereClause += " AND user_id = $" + std::to_string(paramIdx++);
        }

        std::string countQuery = "SELECT COUNT(*) as total FROM oauth2_access_tokens" + whereClause;
        std::string dataQuery =
          "SELECT token, client_id, user_id, scope, issued_at, expires_at "
          "FROM oauth2_access_tokens" +
          whereClause + " ORDER BY issued_at DESC LIMIT " + std::to_string(perPage) + " OFFSET " +
          std::to_string(offset);

        // Execute based on filter combination
        if (!clientIdFilter.empty() && !userIdFilter.empty())
        {
            // Both filters
            db->execSqlAsync(
              countQuery,
              [sharedCb, req, dataQuery, page, perPage, clientIdFilter, userIdFilter, db](
                const ::drogon::orm::Result &countResult
              ) {
                  int total = 0;
                  if (!countResult.empty())
                  {
                      total = countResult[0]["total"].as<int>();
                  }

                  db->execSqlAsync(
                    dataQuery,
                    [sharedCb, req, page, perPage, total](const ::drogon::orm::Result &result) {
                        Json::Value json;
                        Json::Value tokens(Json::arrayValue);

                        for (const auto &row : result)
                        {
                            Json::Value token;
                            std::string fullToken = row["token"].as<std::string>();
                            token["token_prefix"] = fullToken.substr(0, 8);
                            token["client_id"] =
                              row["client_id"].isNull() ? "" : row["client_id"].as<std::string>();
                            token["user_id"] =
                              row["user_id"].isNull() ? "" : row["user_id"].as<std::string>();
                            token["scope"] =
                              row["scope"].isNull() ? "" : row["scope"].as<std::string>();
                            token["created_at"] =
                              row["issued_at"].isNull()
                                ? ""
                                : std::to_string(row["issued_at"].as<int64_t>());
                            token["expires_at"] =
                              row["expires_at"].isNull()
                                ? ""
                                : std::to_string(row["expires_at"].as<int64_t>());
                            tokens.append(token);
                        }

                        json["tokens"] = tokens;
                        json["total"] = total;
                        json["page"] = page;
                        json["per_page"] = perPage;
                        (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                    },
                    [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                        respondError(
                          req,
                          sharedCb,
                          "DB_QUERY_ERROR",
                          std::string("Failed to fetch tokens: ") + e.base().what()
                        );
                    },
                    clientIdFilter,
                    userIdFilter
                  );
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to count tokens: ") + e.base().what()
                  );
              },
              clientIdFilter,
              userIdFilter
            );
        }
        else if (!clientIdFilter.empty())
        {
            // Only client_id filter
            db->execSqlAsync(
              countQuery,
              [sharedCb, req, dataQuery, page, perPage, clientIdFilter, db](
                const ::drogon::orm::Result &countResult
              ) {
                  int total = 0;
                  if (!countResult.empty())
                  {
                      total = countResult[0]["total"].as<int>();
                  }

                  db->execSqlAsync(
                    dataQuery,
                    [sharedCb, req, page, perPage, total](const ::drogon::orm::Result &result) {
                        Json::Value json;
                        Json::Value tokens(Json::arrayValue);

                        for (const auto &row : result)
                        {
                            Json::Value token;
                            std::string fullToken = row["token"].as<std::string>();
                            token["token_prefix"] = fullToken.substr(0, 8);
                            token["client_id"] =
                              row["client_id"].isNull() ? "" : row["client_id"].as<std::string>();
                            token["user_id"] =
                              row["user_id"].isNull() ? "" : row["user_id"].as<std::string>();
                            token["scope"] =
                              row["scope"].isNull() ? "" : row["scope"].as<std::string>();
                            token["created_at"] =
                              row["issued_at"].isNull()
                                ? ""
                                : std::to_string(row["issued_at"].as<int64_t>());
                            token["expires_at"] =
                              row["expires_at"].isNull()
                                ? ""
                                : std::to_string(row["expires_at"].as<int64_t>());
                            tokens.append(token);
                        }

                        json["tokens"] = tokens;
                        json["total"] = total;
                        json["page"] = page;
                        json["per_page"] = perPage;
                        (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                    },
                    [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                        respondError(
                          req,
                          sharedCb,
                          "DB_QUERY_ERROR",
                          std::string("Failed to fetch tokens: ") + e.base().what()
                        );
                    },
                    clientIdFilter
                  );
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to count tokens: ") + e.base().what()
                  );
              },
              clientIdFilter
            );
        }
        else if (!userIdFilter.empty())
        {
            // Only user_id filter
            db->execSqlAsync(
              countQuery,
              [sharedCb, req, dataQuery, page, perPage, userIdFilter, db](
                const ::drogon::orm::Result &countResult
              ) {
                  int total = 0;
                  if (!countResult.empty())
                  {
                      total = countResult[0]["total"].as<int>();
                  }

                  db->execSqlAsync(
                    dataQuery,
                    [sharedCb, req, page, perPage, total](const ::drogon::orm::Result &result) {
                        Json::Value json;
                        Json::Value tokens(Json::arrayValue);

                        for (const auto &row : result)
                        {
                            Json::Value token;
                            std::string fullToken = row["token"].as<std::string>();
                            token["token_prefix"] = fullToken.substr(0, 8);
                            token["client_id"] =
                              row["client_id"].isNull() ? "" : row["client_id"].as<std::string>();
                            token["user_id"] =
                              row["user_id"].isNull() ? "" : row["user_id"].as<std::string>();
                            token["scope"] =
                              row["scope"].isNull() ? "" : row["scope"].as<std::string>();
                            token["created_at"] =
                              row["issued_at"].isNull()
                                ? ""
                                : std::to_string(row["issued_at"].as<int64_t>());
                            token["expires_at"] =
                              row["expires_at"].isNull()
                                ? ""
                                : std::to_string(row["expires_at"].as<int64_t>());
                            tokens.append(token);
                        }

                        json["tokens"] = tokens;
                        json["total"] = total;
                        json["page"] = page;
                        json["per_page"] = perPage;
                        (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                    },
                    [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                        respondError(
                          req,
                          sharedCb,
                          "DB_QUERY_ERROR",
                          std::string("Failed to fetch tokens: ") + e.base().what()
                        );
                    },
                    userIdFilter
                  );
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to count tokens: ") + e.base().what()
                  );
              },
              userIdFilter
            );
        }
        else
        {
            // No filters
            db->execSqlAsync(
              countQuery,
              [sharedCb, req, dataQuery, page, perPage, db](
                const ::drogon::orm::Result &countResult
              ) {
                  int total = 0;
                  if (!countResult.empty())
                  {
                      total = countResult[0]["total"].as<int>();
                  }

                  db->execSqlAsync(
                    dataQuery,
                    [sharedCb, req, page, perPage, total](const ::drogon::orm::Result &result) {
                        Json::Value json;
                        Json::Value tokens(Json::arrayValue);

                        for (const auto &row : result)
                        {
                            Json::Value token;
                            std::string fullToken = row["token"].as<std::string>();
                            token["token_prefix"] = fullToken.substr(0, 8);
                            token["client_id"] =
                              row["client_id"].isNull() ? "" : row["client_id"].as<std::string>();
                            token["user_id"] =
                              row["user_id"].isNull() ? "" : row["user_id"].as<std::string>();
                            token["scope"] =
                              row["scope"].isNull() ? "" : row["scope"].as<std::string>();
                            token["created_at"] =
                              row["issued_at"].isNull()
                                ? ""
                                : std::to_string(row["issued_at"].as<int64_t>());
                            token["expires_at"] =
                              row["expires_at"].isNull()
                                ? ""
                                : std::to_string(row["expires_at"].as<int64_t>());
                            tokens.append(token);
                        }

                        json["tokens"] = tokens;
                        json["total"] = total;
                        json["page"] = page;
                        json["per_page"] = perPage;
                        (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                    },
                    [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                        respondError(
                          req,
                          sharedCb,
                          "DB_QUERY_ERROR",
                          std::string("Failed to fetch tokens: ") + e.base().what()
                        );
                    }
                  );
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to count tokens: ") + e.base().what()
                  );
              }
            );
        }
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::revokeToken(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &tokenPrefix
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (tokenPrefix.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "tokenPrefix is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        std::string likePattern = tokenPrefix + "%";

        db->execSqlAsync(
          "DELETE FROM oauth2_access_tokens WHERE token LIKE $1",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "Token not found");
                  return;
              }

              Json::Value json;
              json["status"] = "success";
              json["message"] = "Token revoked";
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to revoke token: ") + e.base().what()
              );
          },
          likePattern
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::revokeTokensByClient(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("client_id"))
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "Request body must contain 'client_id'"
        );
        return;
    }

    std::string clientId = (*jsonBody)["client_id"].asString();
    if (clientId.empty())
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "client_id cannot be empty"
        );
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();

        // Delete access tokens for this client
        db->execSqlAsync(
          "DELETE FROM oauth2_access_tokens WHERE client_id = $1",
          [sharedCb, req, clientId, db](const ::drogon::orm::Result &accessResult) {
              int accessCount = static_cast<int>(accessResult.affectedRows());

              // Also delete refresh tokens for this client
              db->execSqlAsync(
                "DELETE FROM oauth2_refresh_tokens WHERE client_id = $1",
                [sharedCb, req, clientId, accessCount](const ::drogon::orm::Result &refreshResult) {
                    int totalCount = accessCount + static_cast<int>(refreshResult.affectedRows());

                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "All tokens for client revoked";
                    json["count"] = totalCount;
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                },
                [sharedCb, req, accessCount](const ::drogon::orm::DrogonDbException &) {
                    // Refresh token deletion failed but access tokens were deleted
                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "Access tokens revoked (refresh token cleanup failed)";
                    json["count"] = accessCount;
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                },
                clientId
              );
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to revoke tokens: ") + e.base().what()
              );
          },
          clientId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::revokeTokensByUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("user_id"))
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "Request body must contain 'user_id'"
        );
        return;
    }

    std::string userId = (*jsonBody)["user_id"].asString();
    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "user_id cannot be empty");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();

        // Delete access tokens for this user
        db->execSqlAsync(
          "DELETE FROM oauth2_access_tokens WHERE user_id = $1",
          [sharedCb, req, userId, db](const ::drogon::orm::Result &accessResult) {
              int accessCount = static_cast<int>(accessResult.affectedRows());

              // Also delete refresh tokens for this user
              db->execSqlAsync(
                "DELETE FROM oauth2_refresh_tokens WHERE user_id = $1",
                [sharedCb, req, userId, accessCount](const ::drogon::orm::Result &refreshResult) {
                    int totalCount = accessCount + static_cast<int>(refreshResult.affectedRows());

                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "All tokens for user revoked";
                    json["count"] = totalCount;
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                },
                [sharedCb, req, accessCount](const ::drogon::orm::DrogonDbException &) {
                    // Refresh token deletion failed but access tokens were deleted
                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "Access tokens revoked (refresh token cleanup failed)";
                    json["count"] = accessCount;
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                },
                userId
              );
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to revoke tokens: ") + e.base().what()
              );
          },
          userId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::getOidcKeys(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    Json::Value json;
    json["status"] = "success";
    json["kid"] = "default-key-1";
    json["kty"] = "RSA";
    json["alg"] = "RS256";
    json["use"] = "sig";
    json["jwks_uri"] = "/.well-known/jwks.json";
    json["discovery_uri"] = "/.well-known/openid-configuration";
    json["key_status"] = "active";
    json["note"] = "Key rotation is not yet implemented. Single signing key in use.";

    callback(::drogon::HttpResponse::newHttpJsonResponse(json));
}

// ============================================================
// User Detail & Management
// ============================================================

void AdminController::getUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "userId is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT u.id, u.username, u.email, u.email_verified, u.mfa_enabled, "
          "u.failed_login_count, u.locked_until, u.created_at, "
          "COALESCE(json_agg(r.name) FILTER (WHERE r.name IS NOT NULL), '[]') AS roles "
          "FROM users u "
          "LEFT JOIN user_roles ur ON u.id = ur.user_id "
          "LEFT JOIN roles r ON ur.role_id = r.id "
          "WHERE u.id = $1 "
          "GROUP BY u.id",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              if (result.empty())
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found");
                  return;
              }
              const auto &row = result[0];
              Json::Value json;
              json["status"] = "success";
              json["id"] = row["id"].as<int>();
              json["username"] = row["username"].as<std::string>();
              json["email"] = row["email"].isNull() ? "" : row["email"].as<std::string>();
              json["email_verified"] =
                row["email_verified"].isNull() ? false : row["email_verified"].as<bool>();
              json["mfa_enabled"] =
                row["mfa_enabled"].isNull() ? false : row["mfa_enabled"].as<bool>();
              json["failed_login_count"] =
                row["failed_login_count"].isNull() ? 0 : row["failed_login_count"].as<int>();
              int64_t lockedUntil =
                row["locked_until"].isNull() ? 0 : row["locked_until"].as<int64_t>();
              auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch()
              )
                           .count();
              json["locked"] = (lockedUntil > now);
              json["locked_until"] = lockedUntil;
              json["created_at"] =
                row["created_at"].isNull() ? "" : row["created_at"].as<std::string>();
              // Parse roles JSON array from aggregation
              std::string rolesStr = row["roles"].isNull() ? "[]" : row["roles"].as<std::string>();
              Json::Value rolesJson;
              Json::CharReaderBuilder builder;
              std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
              std::string parseErrors;
              if (
                reader->parse(
                  rolesStr.c_str(), rolesStr.c_str() + rolesStr.size(), &rolesJson, &parseErrors
                ) &&
                rolesJson.isArray()
              )
              {
                  json["roles"] = rolesJson;
              }
              else
              {
                  json["roles"] = Json::Value(Json::arrayValue);
              }
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch user: ") + e.base().what()
              );
          },
          userId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::updateUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "userId is required");
        return;
    }

    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "Invalid JSON body");
        return;
    }

    std::vector<std::string> setClauses;
    std::vector<std::string> params;
    int paramIdx = 1;

    if (jsonBody->isMember("email"))
    {
        // Normalize on write so admin edits stay consistent with registration
        // (login + password reset look up the canonical form).
        setClauses.push_back("email = $" + std::to_string(paramIdx++));
        params.push_back(::oauth2::utils::normalizeEmail((*jsonBody)["email"].asString()));
    }
    if (jsonBody->isMember("email_verified"))
    {
        setClauses.push_back("email_verified = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["email_verified"].asBool() ? "true" : "false");
    }

    if (setClauses.empty())
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "No updatable fields provided");
        return;
    }

    std::string query = "UPDATE users SET ";
    for (size_t i = 0; i < setClauses.size(); ++i)
    {
        if (i > 0)
            query += ", ";
        query += setClauses[i];
    }
    query += " WHERE id = $" + std::to_string(paramIdx);
    params.push_back(userId);

    try
    {
        auto db = ::drogon::app().getDbClient();
        if (params.size() == 2)
        {
            db->execSqlAsync(
              query,
              [sharedCb, req, userId](const ::drogon::orm::Result &result) {
                  if (result.affectedRows() == 0)
                  {
                      respondError(
                        req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found"
                      );
                      return;
                  }
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "User updated successfully";
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to update user: ") + e.base().what()
                  );
              },
              params[0],
              params[1]
            );
        }
        else if (params.size() == 3)
        {
            db->execSqlAsync(
              query,
              [sharedCb, req](const ::drogon::orm::Result &result) {
                  if (result.affectedRows() == 0)
                  {
                      respondError(
                        req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found"
                      );
                      return;
                  }
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "User updated successfully";
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to update user: ") + e.base().what()
                  );
              },
              params[0],
              params[1],
              params[2]
            );
        }
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::enableUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "userId is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "UPDATE users SET locked_until = 0, failed_login_count = 0 WHERE id = $1",
          [sharedCb, req, userId](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found");
                  return;
              }
              Json::Value json;
              json["status"] = "success";
              json["message"] = "User enabled successfully";
              json["user_id"] = userId;
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to enable user: ") + e.base().what()
              );
          },
          userId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::getUserRoles(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "userId is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT r.id, r.name, r.description FROM roles r "
          "JOIN user_roles ur ON r.id = ur.role_id "
          "WHERE ur.user_id = $1 ORDER BY r.name",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              Json::Value json;
              json["status"] = "success";
              Json::Value roles(Json::arrayValue);
              for (const auto &row : result)
              {
                  Json::Value role;
                  role["id"] = row["id"].as<int>();
                  role["name"] = row["name"].as<std::string>();
                  role["description"] =
                    row["description"].isNull() ? "" : row["description"].as<std::string>();
                  roles.append(role);
              }
              json["roles"] = roles;
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch user roles: ") + e.base().what()
              );
          },
          userId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

// ============================================================
// Role Management
// ============================================================

void AdminController::listRoles(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT r.id, r.name, r.description, r.created_at, "
          "COUNT(DISTINCT ur.user_id) AS user_count "
          "FROM roles r "
          "LEFT JOIN user_roles ur ON r.id = ur.role_id "
          "GROUP BY r.id ORDER BY r.name",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              Json::Value json;
              json["status"] = "success";
              Json::Value roles(Json::arrayValue);
              for (const auto &row : result)
              {
                  Json::Value role;
                  role["id"] = row["id"].as<int>();
                  role["name"] = row["name"].as<std::string>();
                  role["description"] =
                    row["description"].isNull() ? "" : row["description"].as<std::string>();
                  role["user_count"] = row["user_count"].as<int>();
                  role["created_at"] =
                    row["created_at"].isNull() ? "" : row["created_at"].as<std::string>();
                  roles.append(role);
              }
              json["roles"] = roles;
              json["total"] = static_cast<int>(result.size());
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch roles: ") + e.base().what()
              );
          }
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::createRole(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("name"))
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "Request body must contain 'name'"
        );
        return;
    }

    std::string name = (*jsonBody)["name"].asString();
    std::string description = jsonBody->get("description", "").asString();

    if (name.empty())
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "Role name cannot be empty"
        );
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        // Check for existing role first to return proper 409
        db->execSqlAsync(
          "SELECT id FROM roles WHERE name = $1",
          [sharedCb, req, db, name, description](const ::drogon::orm::Result &checkResult) {
              if (!checkResult.empty())
              {
                  respondError(
                    req, sharedCb, "VALIDATION_RESOURCE_CONFLICT", "Role name already exists"
                  );
                  return;
              }
              db->execSqlAsync(
                "INSERT INTO roles (name, description) VALUES ($1, $2) "
                "RETURNING id, name, description",
                [sharedCb, req](const ::drogon::orm::Result &result) {
                    if (result.empty())
                    {
                        respondError(req, sharedCb, "INTERNAL_ERROR", "Failed to create role");
                        return;
                    }
                    const auto &row = result[0];
                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "Role created successfully";
                    json["id"] = row["id"].as<int>();
                    json["name"] = row["name"].as<std::string>();
                    json["description"] =
                      row["description"].isNull() ? "" : row["description"].as<std::string>();
                    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                    resp->setStatusCode(::drogon::k201Created);
                    (*sharedCb)(resp);
                },
                [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                    respondError(
                      req,
                      sharedCb,
                      "DB_QUERY_ERROR",
                      std::string("Failed to create role: ") + e.base().what()
                    );
                },
                name,
                description
              );
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Database error checking role name: ") + e.base().what()
              );
          },
          name
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::updateRole(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &roleId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "Invalid JSON body");
        return;
    }

    std::vector<std::string> setClauses;
    std::vector<std::string> params;
    int paramIdx = 1;

    if (jsonBody->isMember("description"))
    {
        setClauses.push_back("description = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["description"].asString());
    }

    if (setClauses.empty())
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "No updatable fields provided");
        return;
    }

    std::string query = "UPDATE roles SET ";
    for (size_t i = 0; i < setClauses.size(); ++i)
    {
        if (i > 0)
            query += ", ";
        query += setClauses[i];
    }
    query += " WHERE id = $" + std::to_string(paramIdx);
    params.push_back(roleId);

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          query,
          [sharedCb, req](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "Role not found");
                  return;
              }
              Json::Value json;
              json["status"] = "success";
              json["message"] = "Role updated successfully";
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to update role: ") + e.base().what()
              );
          },
          params[0],
          params[1]
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::deleteRole(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &roleId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "DELETE FROM roles WHERE id = $1 AND name NOT IN ('admin', 'user')",
          [sharedCb, req, roleId](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(
                    req,
                    sharedCb,
                    "VALIDATION_RESOURCE_NOT_FOUND",
                    "Role not found or cannot delete built-in roles"
                  );
                  return;
              }
              Json::Value json;
              json["status"] = "success";
              json["message"] = "Role deleted successfully";
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to delete role: ") + e.base().what()
              );
          },
          roleId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

// ============================================================
// Scope Management (CRUD)
// ============================================================

void AdminController::createScope(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("name"))
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "Request body must contain 'name'"
        );
        return;
    }

    std::string name = (*jsonBody)["name"].asString();
    std::string description = jsonBody->get("description", "").asString();
    std::string mappedRole = jsonBody->get("mapped_role", "").asString();
    bool isDefault = jsonBody->get("is_default", false).asBool();
    bool requiresAdminRole = jsonBody->get("requires_admin_role", false).asBool();

    if (name.empty())
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "Scope name cannot be empty"
        );
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT id FROM oauth2_scopes WHERE name = $1",
          [sharedCb, req, db, name, description, mappedRole, isDefault, requiresAdminRole](
            const ::drogon::orm::Result &checkResult
          ) {
              if (!checkResult.empty())
              {
                  respondError(
                    req, sharedCb, "VALIDATION_RESOURCE_CONFLICT", "Scope name already exists"
                  );
                  return;
              }
              db->execSqlAsync(
                "INSERT INTO oauth2_scopes (name, description, mapped_role, is_default, "
                "requires_admin_role) VALUES ($1, $2, $3, $4, $5) "
                "RETURNING id, name, description, mapped_role, is_default, requires_admin_role",
                [sharedCb, req](const ::drogon::orm::Result &result) {
                    if (result.empty())
                    {
                        respondError(req, sharedCb, "INTERNAL_ERROR", "Failed to create scope");
                        return;
                    }
                    const auto &row = result[0];
                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "Scope created successfully";
                    json["id"] = row["id"].as<int>();
                    json["name"] = row["name"].as<std::string>();
                    json["description"] =
                      row["description"].isNull() ? "" : row["description"].as<std::string>();
                    json["mapped_role"] =
                      row["mapped_role"].isNull() ? "" : row["mapped_role"].as<std::string>();
                    json["is_default"] =
                      row["is_default"].isNull() ? false : row["is_default"].as<bool>();
                    json["requires_admin_role"] = row["requires_admin_role"].isNull()
                                                    ? false
                                                    : row["requires_admin_role"].as<bool>();
                    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                    resp->setStatusCode(::drogon::k201Created);
                    (*sharedCb)(resp);
                },
                [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                    respondError(
                      req,
                      sharedCb,
                      "DB_QUERY_ERROR",
                      std::string("Failed to create scope: ") + e.base().what()
                    );
                },
                name,
                description,
                mappedRole.empty() ? nullptr : mappedRole.c_str(),
                isDefault,
                requiresAdminRole
              );
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Database error checking scope name: ") + e.base().what()
              );
          },
          name
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::updateScope(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &scopeId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "Invalid JSON body");
        return;
    }

    std::vector<std::string> setClauses;
    std::vector<std::string> params;
    int paramIdx = 1;

    if (jsonBody->isMember("description"))
    {
        setClauses.push_back("description = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["description"].asString());
    }
    if (jsonBody->isMember("mapped_role"))
    {
        setClauses.push_back("mapped_role = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["mapped_role"].asString());
    }
    if (jsonBody->isMember("is_default"))
    {
        setClauses.push_back("is_default = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["is_default"].asBool() ? "true" : "false");
    }
    if (jsonBody->isMember("requires_admin_role"))
    {
        setClauses.push_back("requires_admin_role = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["requires_admin_role"].asBool() ? "true" : "false");
    }

    if (setClauses.empty())
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "No updatable fields provided");
        return;
    }

    std::string query = "UPDATE oauth2_scopes SET ";
    for (size_t i = 0; i < setClauses.size(); ++i)
    {
        if (i > 0)
            query += ", ";
        query += setClauses[i];
    }
    query += " WHERE id = $" + std::to_string(paramIdx);
    params.push_back(scopeId);

    try
    {
        auto db = ::drogon::app().getDbClient();
        // Use a lambda that captures params by value and dispatches based on count
        auto execUpdate = [&](auto &&...args) {
            db->execSqlAsync(
              query,
              [sharedCb, req](const ::drogon::orm::Result &result) {
                  if (result.affectedRows() == 0)
                  {
                      respondError(
                        req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "Scope not found"
                      );
                      return;
                  }
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "Scope updated successfully";
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to update scope: ") + e.base().what()
                  );
              },
              std::forward<decltype(args)>(args)...
            );
        };

        if (params.size() == 2)
            execUpdate(params[0], params[1]);
        else if (params.size() == 3)
            execUpdate(params[0], params[1], params[2]);
        else if (params.size() == 4)
            execUpdate(params[0], params[1], params[2], params[3]);
        else if (params.size() == 5)
            execUpdate(params[0], params[1], params[2], params[3], params[4]);
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void AdminController::deleteScope(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &scopeId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "DELETE FROM oauth2_scopes WHERE id = $1 "
          "AND name NOT IN ('openid', 'profile', 'email', 'admin')",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(
                    req,
                    sharedCb,
                    "VALIDATION_RESOURCE_NOT_FOUND",
                    "Scope not found or cannot delete built-in scopes"
                  );
                  return;
              }
              Json::Value json;
              json["status"] = "success";
              json["message"] = "Scope deleted successfully";
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to delete scope: ") + e.base().what()
              );
          },
          scopeId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

// ============================================================
// Dashboard Stats
// ============================================================

void AdminController::getDashboardStats(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch()
        )
                     .count();
        auto dayAgo = now - 86400;

        // Query all stats in parallel using a single compound query
        db->execSqlAsync(
          "SELECT "
          "(SELECT COUNT(*) FROM users) AS total_users, "
          "(SELECT COUNT(*) FROM oauth2_clients) AS total_clients, "
          "(SELECT COUNT(*) FROM oauth2_access_tokens "
          " WHERE expires_at > $1 AND (revoked IS NULL OR revoked = FALSE)) AS active_tokens, "
          "(SELECT COUNT(*) FROM audit_logs WHERE timestamp > to_timestamp($2)) AS logs_today, "
          "(SELECT COUNT(*) FROM audit_logs WHERE outcome = 'failure' "
          " AND timestamp > to_timestamp($3)) AS failures_today",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              if (result.empty())
              {
                  respondError(req, sharedCb, "INTERNAL_ERROR", "Failed to fetch stats");
                  return;
              }
              const auto &row = result[0];
              Json::Value json;
              json["status"] = "success";
              json["total_users"] = row["total_users"].as<int>();
              json["total_clients"] = row["total_clients"].as<int>();
              json["active_tokens"] = row["active_tokens"].as<int>();
              json["logs_today"] = row["logs_today"].as<int>();
              json["failures_today"] = row["failures_today"].as<int>();
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch dashboard stats: ") + e.base().what()
              );
          },
          now,
          dayAgo,
          dayAgo
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

// ========== Dashboard (merged from old AdminController) ==========

void AdminController::dashboard(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    Json::Value json;
    json["message"] = "Welcome to Admin Dashboard";
    json["status"] = "success";

    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
    callback(resp);
}

}  // namespace authforge::drogon::controllers
