#include <authforge/drogon/controllers/AdminController.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <oauth2/utils/CryptoUtils.h>
#include <oauth2/utils/EmailNormalizer.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
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
      req,
      [cb](const ::drogon::HttpResponsePtr &r) { (*cb)(r); },
      std::move(code),
      std::move(detailForLog)
    );
}

struct AdminApiControllerDocs
{
    AdminApiControllerDocs()
    {
        // Client Management endpoint docs moved to ClientAdminControllerDocs
        // (M5 Task 29a).
        // User Management endpoint docs moved to UserAdminControllerDocs
        // (M5 Task 29a).

        ::authforge::drogon::observability::openapi::EndpointInfo listScopes;
        listScopes.path = "/api/admin/scopes";
        listScopes.method = "GET";
        listScopes.summary = "List Scopes";
        listScopes.description = "Get a list of all available scopes.";
        listScopes.tags = {"Admin", "Scopes"};
        listScopes.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(listScopes);

        ::authforge::drogon::observability::openapi::EndpointInfo listLogs;
        listLogs.path = "/api/admin/logs";
        listLogs.method = "GET";
        listLogs.summary = "List Audit Logs";
        listLogs.description = "Get a paginated list of system audit logs.";
        listLogs.tags = {"Admin", "Logs"};
        listLogs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(listLogs);

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

        // User detail/role endpoint docs moved to UserAdminControllerDocs
        // (M5 Task 29a).

        ::authforge::drogon::observability::openapi::EndpointInfo listRoles;
        listRoles.path = "/api/admin/roles";
        listRoles.method = "GET";
        listRoles.summary = "List Roles";
        listRoles.description = "Get a list of all roles with user counts.";
        listRoles.tags = {"Admin", "Roles"};
        listRoles.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(listRoles);

        ::authforge::drogon::observability::openapi::EndpointInfo createRole;
        createRole.path = "/api/admin/roles";
        createRole.method = "POST";
        createRole.summary = "Create Role";
        createRole.description = "Create a new role. Built-in roles cannot be duplicated.";
        createRole.tags = {"Admin", "Roles"};
        createRole.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(createRole);

        ::authforge::drogon::observability::openapi::EndpointInfo updateRole;
        updateRole.path = "/api/admin/roles/{roleId}";
        updateRole.method = "PUT";
        updateRole.summary = "Update Role";
        updateRole.description = "Update a role's description.";
        updateRole.tags = {"Admin", "Roles"};
        updateRole.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(updateRole);

        ::authforge::drogon::observability::openapi::EndpointInfo deleteRole;
        deleteRole.path = "/api/admin/roles/{roleId}";
        deleteRole.method = "DELETE";
        deleteRole.summary = "Delete Role";
        deleteRole.description = "Delete a role. Built-in roles (admin, user) cannot be deleted.";
        deleteRole.tags = {"Admin", "Roles"};
        deleteRole.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(deleteRole);

        ::authforge::drogon::observability::openapi::EndpointInfo createScope;
        createScope.path = "/api/admin/scopes";
        createScope.method = "POST";
        createScope.summary = "Create Scope";
        createScope.description = "Create a new OAuth2 scope.";
        createScope.tags = {"Admin", "Scopes"};
        createScope.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(createScope);

        ::authforge::drogon::observability::openapi::EndpointInfo updateScope;
        updateScope.path = "/api/admin/scopes/{scopeId}";
        updateScope.method = "PUT";
        updateScope.summary = "Update Scope";
        updateScope.description = "Update a scope's properties.";
        updateScope.tags = {"Admin", "Scopes"};
        updateScope.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(updateScope);

        ::authforge::drogon::observability::openapi::EndpointInfo deleteScope;
        deleteScope.path = "/api/admin/scopes/{scopeId}";
        deleteScope.method = "DELETE";
        deleteScope.summary = "Delete Scope";
        deleteScope.description = "Delete a scope. Built-in scopes cannot be deleted.";
        deleteScope.tags = {"Admin", "Scopes"};
        deleteScope.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(deleteScope);

        ::authforge::drogon::observability::openapi::EndpointInfo getDashboardStats;
        getDashboardStats.path = "/api/admin/dashboard/stats";
        getDashboardStats.method = "GET";
        getDashboardStats.summary = "Get Dashboard Stats";
        getDashboardStats.description =
          "Get dashboard statistics including user count, client count, active tokens, and failure "
          "metrics.";
        getDashboardStats.tags = {"Admin", "Dashboard"};
        getDashboardStats.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(
          getDashboardStats
        );
    }
};

AdminApiControllerDocs docs_;
}  // namespace

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
