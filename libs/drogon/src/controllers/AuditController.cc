#include <authforge/drogon/controllers/AuditController.h>
#include <drogon/drogon.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <chrono>
#include <memory>

// M5 Task 29a (authforge-sdk-refactor): audit-log + dashboard routes moved
// verbatim from AdminController.cc (the final 3 routes; AdminController is now
// empty and removed). respondError helper, OpenAPI docs struct, and every
// handler body are byte-for-byte copies -- no behavior change (Admin API tests
// must stay green). The raw SQL and business logic remain inline pending
// Task 29b extraction.

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

struct AuditControllerDocs
{
    AuditControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo listLogs;
        listLogs.path = "/api/admin/logs";
        listLogs.method = "GET";
        listLogs.summary = "List Audit Logs";
        listLogs.description = "Get a paginated list of system audit logs.";
        listLogs.tags = {"Admin", "Logs"};
        listLogs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(listLogs);

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
} g_auditControllerDocs;
}  // namespace

void AuditController::listLogs(
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

void AuditController::getDashboardStats(
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

void AuditController::dashboard(
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
