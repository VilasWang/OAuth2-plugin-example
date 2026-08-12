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
namespace openapi = ::authforge::drogon::observability::openapi;

// #43 resource-scope authorization: declare one EndpointInfo with its
// requiredScopes + impliedBy. All audit/dashboard routes are admin-gated; the
// `admin` super-scope (in impliedBy) satisfies any of them. `tags` is a
// parameter because this controller mixes the Logs and Dashboard tag groups.
openapi::EndpointInfo adminEp(
  const char *path,
  const char *method,
  const char *summary,
  const char *description,
  std::vector<std::string> tags,
  std::vector<std::string> requiredScopes)
{
    openapi::EndpointInfo ep;
    ep.path = path;
    ep.method = method;
    ep.summary = summary;
    ep.description = description;
    ep.tags = std::move(tags);
    ep.requiresAuth = true;
    ep.requiredScopes = std::move(requiredScopes);
    ep.impliedBy = {"admin"};
    return ep;
}
}  // namespace

void AuditController::initApiDocs()
{
    static std::once_flag docsOnce;
    std::call_once(docsOnce, [] { initApiDocsImpl(); });
}

void AuditController::initApiDocsImpl()
{
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/dashboard", "GET", "Get Dashboard",
              "Get the admin dashboard overview page.", {"Admin", "Dashboard"}, {"audit:read"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/logs", "GET", "List Audit Logs",
              "Get a paginated list of system audit logs.", {"Admin", "Logs"}, {"audit:read"}));
    openapi::OpenApiGenerator::addEndpoint(
      adminEp("/api/admin/dashboard/stats", "GET", "Get Dashboard Stats",
              "Get dashboard statistics including user count, client count, active tokens, and "
              "failure metrics.",
              {"Admin", "Dashboard"}, {"audit:read"}));
}

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
