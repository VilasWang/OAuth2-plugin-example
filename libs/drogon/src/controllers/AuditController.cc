#include <authforge/drogon/controllers/AuditController.h>
#include <authforge/drogon/admin/AuditService.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>

#include <memory>

// M5 Task 29b batch 6 (authforge-sdk-refactor): inline raw-SQL DB access from
// the Task 29a verbatim move is now delegated to AuditService (Mapper + Criteria,
// per .claude/rules/db-operations.md). The 5-subquery compound dashboard query
// is split into 5 sequential Mapper::count() calls. Controller is now a thin
// HTTP adapter. The audit_logs ORM model was added in this batch (regenerated).

namespace authforge::drogon::controllers
{

namespace
{
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

using AuditSvc = ::authforge::drogon::admin::AuditService;

void AuditController::listLogs(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    AuditSvc::listLogs(req, sharedCb);
}

void AuditController::getDashboardStats(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    AuditSvc::getDashboardStats(req, sharedCb);
}

void AuditController::dashboard(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    (void)req;  // static welcome route, no auth-derived behavior
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    AuditSvc::dashboard(sharedCb);
}

}  // namespace authforge::drogon::controllers
