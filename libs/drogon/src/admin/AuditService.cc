#include <fulla/drogon/admin/AuditService.h>

#include <fulla/storage/postgres/models/AuditLogs.h>
#include <fulla/storage/postgres/models/Users.h>
#include <fulla/storage/postgres/models/Oauth2Clients.h>
#include <fulla/storage/postgres/models/Oauth2AccessTokens.h>
#include <fulla/drogon/error/ErrorResponder.h>

#include <drogon/drogon.h>
#include <trantor/utils/Date.h>

#include <chrono>

namespace fulla::drogon::admin
{

namespace
{
void respondError(
  const ::drogon::HttpRequestPtr &req,
  const AuditService::ResponseCallback &cb,
  std::string code,
  std::string detailForLog = ""
)
{
    ::fulla::common::error::ErrorResponder::respond(
      req,
      [cb](const ::drogon::HttpResponsePtr &r) { (*cb)(r); },
      std::move(code),
      std::move(detailForLog)
    );
}

using namespace ::drogon::orm;
using namespace ::drogon_model::fulla_db;

::drogon::orm::DbClientPtr getDbOrRespond(
  const ::drogon::HttpRequestPtr &req,
  const AuditService::ResponseCallback &cb
)
{
    try
    {
        return ::drogon::app().getDbClient();
    }
    catch (...)
    {
        respondError(req, cb, "DB_CONNECTION_ERROR", "Database unavailable");
        return nullptr;
    }
}

Json::Value logRowToJson(const AuditLogs &row)
{
    Json::Value log;
    log["id"] = row.getValueOfId();
    log["timestamp"] = row.getValueOfTimestamp().toDbString();
    log["actor_type"] = row.getValueOfActorType();
    log["actor_id"] = row.getValueOfActorId();
    log["action"] = row.getValueOfAction();
    log["target_type"] = row.getValueOfTargetType();
    log["target_id"] = row.getValueOfTargetId();
    log["outcome"] = row.getValueOfOutcome();
    log["ip"] = row.getValueOfIp();
    return log;
}
}  // namespace

void AuditService::listLogs(const ::drogon::HttpRequestPtr &req, ResponseCallback cb)
{
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

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    // Build the optional filters as a compound Criteria (the previous
    // implementation built the clause then ran an unfiltered query -- A-LOG-004;
    // this version actually applies the filters).
    Criteria filter;
    if (!action.empty())
    {
        filter = filter && Criteria(AuditLogs::Cols::_action, CompareOperator::EQ, action);
    }
    if (!outcome.empty())
    {
        filter = filter && Criteria(AuditLogs::Cols::_outcome, CompareOperator::EQ, outcome);
    }
    if (!actorId.empty())
    {
        filter = filter && Criteria(AuditLogs::Cols::_actor_id, CompareOperator::EQ, actorId);
    }

    // #146: count-then-page two-step (same shape as
    // TokenManagementService::listTokens). `total` must be the number of rows
    // matching the filter set -- the previous implementation echoed back the
    // current page's row count, so any client paging via `total` mis-computed.
    // Each Mapper construction gets its own try/catch (db-operations rule:
    // an outer guard cannot protect constructions inside async callbacks).
    try
    {
        Mapper<AuditLogs> countMapper(db);
        countMapper.count(
          filter,
          [cb, req, page, perPage, filter, db](const size_t total) {
              try
              {
                  Mapper<AuditLogs> dataMapper(db);
                  dataMapper.paginate(page, perPage)
                    .orderBy(AuditLogs::Cols::_timestamp, SortOrder::DESC)
                    .findBy(
                      filter,
                      [cb, page, perPage, total](const std::vector<AuditLogs> &rows) {
                          Json::Value json;
                          json["status"] = "success";
                          json["page"] = page;
                          json["per_page"] = perPage;
                          json["total"] = static_cast<Json::UInt64>(total);
                          Json::Value logs(Json::arrayValue);
                          for (const auto &row : rows)
                          {
                              logs.append(logRowToJson(row));
                          }
                          json["logs"] = logs;
                          (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                      },
                      [req, cb](const ::drogon::orm::DrogonDbException &e) {
                          respondError(
                            req,
                            cb,
                            "DB_QUERY_ERROR",
                            std::string("Failed to fetch audit logs: ") + e.base().what()
                          );
                      }
                    );
              }
              catch (const std::exception &e)
              {
                  respondError(
                    req,
                    cb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to fetch audit logs: ") + e.what()
                  );
              }
          },
          [req, cb](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                cb,
                "DB_QUERY_ERROR",
                std::string("Failed to count audit logs: ") + e.base().what()
              );
          }
        );
    }
    catch (const std::exception &e)
    {
        respondError(
          req,
          cb,
          "DB_QUERY_ERROR",
          std::string("Failed to count audit logs: ") + e.what()
        );
    }
}

void AuditService::getDashboardStats(const ::drogon::HttpRequestPtr &req, ResponseCallback cb)
{
    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    // Original: a single compound query with 5 (SELECT COUNT(*)) subqueries.
    // Split into 5 sequential Mapper::count() calls (one per stat). The
    // active-token and logs/failures "today" filters use the same conditions
    // (now/dayAgo evaluated in C++ for the epoch comparisons, matching the
    // other admin services' approach).
    int64_t now = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::system_clock::now().time_since_epoch()
    )
                                         .count());
    int64_t dayAgo = now - 86400;

    // Stats accumulator shared across the count chain.
    struct Stats
    {
        size_t totalUsers = 0;
        size_t totalClients = 0;
        size_t activeTokens = 0;
        size_t logsToday = 0;
        size_t failuresToday = 0;
    };

    auto stats = std::make_shared<Stats>();

    // Chain: totalUsers -> totalClients -> activeTokens -> logsToday ->
    // failuresToday -> respond.
    Mapper<Users>(db).count(
      Criteria(),
      [cb, req, db, stats, dayAgo, now](const size_t n) {
          stats->totalUsers = n;
          Mapper<Oauth2Clients>(db).count(
            Criteria(),
            [cb, req, db, stats, dayAgo, now](const size_t n2) {
                stats->totalClients = n2;
                Criteria activeTok =
                  Criteria(Oauth2AccessTokens::Cols::_expires_at, CompareOperator::GT, now) &&
                  (Criteria(Oauth2AccessTokens::Cols::_revoked, CompareOperator::EQ, false) ||
                   Criteria(Oauth2AccessTokens::Cols::_revoked, CompareOperator::IsNull));
                Mapper<Oauth2AccessTokens>(db).count(
                  activeTok,
                  [cb, req, db, stats, dayAgo](const size_t n3) {
                      stats->activeTokens = n3;
                      // AuditLogs.timestamp is a trantor::Date; the original
                      // used to_timestamp($epoch) on the DB side. Compare
                      // against a Date built from the same epoch (microseconds).
                      Criteria logsToday = Criteria(
                        AuditLogs::Cols::_timestamp,
                        CompareOperator::GT,
                        ::trantor::Date(dayAgo * 1000000)
                      );
                      Mapper<AuditLogs>(db).count(
                        logsToday,
                        [cb, req, db, stats](const size_t n4) {
                            stats->logsToday = n4;
                            Criteria failuresToday =
                              Criteria(AuditLogs::Cols::_outcome, CompareOperator::EQ, "failure");
                            Mapper<AuditLogs>(db).count(
                              failuresToday,
                              [cb, stats](const size_t n5) {
                                  stats->failuresToday = n5;
                                  Json::Value json;
                                  json["status"] = "success";
                                  json["total_users"] =
                                    static_cast<Json::UInt64>(stats->totalUsers);
                                  json["total_clients"] =
                                    static_cast<Json::UInt64>(stats->totalClients);
                                  json["active_tokens"] =
                                    static_cast<Json::UInt64>(stats->activeTokens);
                                  json["logs_today"] = static_cast<Json::UInt64>(stats->logsToday);
                                  json["failures_today"] =
                                    static_cast<Json::UInt64>(stats->failuresToday);
                                  (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                              },
                              [req, cb](const ::drogon::orm::DrogonDbException &e) {
                                  respondError(
                                    req,
                                    cb,
                                    "DB_QUERY_ERROR",
                                    std::string("Failed to fetch dashboard stats: ") +
                                      e.base().what()
                                  );
                              }
                            );
                        },
                        [req, cb](const ::drogon::orm::DrogonDbException &e) {
                            respondError(
                              req,
                              cb,
                              "DB_QUERY_ERROR",
                              std::string("Failed to fetch dashboard stats: ") + e.base().what()
                            );
                        }
                      );
                  },
                  [req, cb](const ::drogon::orm::DrogonDbException &e) {
                      respondError(
                        req,
                        cb,
                        "DB_QUERY_ERROR",
                        std::string("Failed to fetch dashboard stats: ") + e.base().what()
                      );
                  }
                );
            },
            [req, cb](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req,
                  cb,
                  "DB_QUERY_ERROR",
                  std::string("Failed to fetch dashboard stats: ") + e.base().what()
                );
            }
          );
      },
      [req, cb](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req,
            cb,
            "DB_QUERY_ERROR",
            std::string("Failed to fetch dashboard stats: ") + e.base().what()
          );
      }
    );
}

void AuditService::dashboard(ResponseCallback cb)
{
    Json::Value json;
    json["message"] = "Welcome to Admin Dashboard";
    json["status"] = "success";
    (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
}

}  // namespace fulla::drogon::admin
