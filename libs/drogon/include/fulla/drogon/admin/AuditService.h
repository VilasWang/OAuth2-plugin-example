#pragma once

// M5 Task 29b batch 6 (fulla-sdk-refactor): application-service extraction
// for the audit/dashboard admin domain. Raw SQL inline in AuditController is
// now Mapper<T> + Criteria on the ORM AuditLogs/Users/Oauth2Clients/
// Oauth2AccessTokens models (per .claude/rules/db-operations.md). The compound
// 5-subquery dashboard query is split into separate Mapper::count() calls.
// Behavior equivalent (Admin API tests must stay green).
//
// Lives in libs/drogon (Adapter/SDK layer, namespace fulla::drogon::admin).
//
// NOTE (batch 6): required regenerating the ORM model set to include the
// `audit_logs` table (previously absent from model.json). AuditLogs.h/.cc are
// now generated alongside the other models.

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>
#include <memory>

namespace fulla::drogon::admin
{

/**
 * @brief Application service for audit-log listing + dashboard stats.
 */
class AuditService
{
  public:
    using ResponseCallback =
      std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>>;

    // ---- GET /api/admin/logs (paginated, filterable) ----
    static void listLogs(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);

    // ---- GET /api/admin/dashboard/stats ----
    static void getDashboardStats(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);

    // ---- GET /api/admin/dashboard (no DB, static welcome) ----
    static void dashboard(ResponseCallback cb);
};

}  // namespace fulla::drogon::admin
