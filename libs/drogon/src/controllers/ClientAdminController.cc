#include <authforge/drogon/controllers/ClientAdminController.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <oauth2/utils/CryptoUtils.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <atomic>
#include <mutex>

// M5 Task 29a (authforge-sdk-refactor): client-management routes moved
// verbatim from AdminController.cc. The respondError helper, OpenAPI docs
// struct, and every handler body are byte-for-byte copies -- no behavior
// change (Admin API tests must stay green). The raw SQL and business logic
// remain inline pending Task 29b extraction.

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
      req,
      [cb](const ::drogon::HttpResponsePtr &r) { (*cb)(r); },
      std::move(code),
      std::move(detailForLog)
    );
}

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

void ClientAdminController::listClients(
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

void ClientAdminController::createClient(
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

void ClientAdminController::getClient(
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

void ClientAdminController::updateClient(
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

void ClientAdminController::deleteClient(
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

void ClientAdminController::resetClientSecret(
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

void ClientAdminController::getClientScopes(
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

void ClientAdminController::updateClientScopes(
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

}  // namespace authforge::drogon::controllers
