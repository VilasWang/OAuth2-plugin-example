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
        // Role + Scope endpoint docs moved to RoleScopeAdminControllerDocs
        // (M5 Task 29a).

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
