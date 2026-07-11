#pragma once

// M3 Task 20 slice 7 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/AdminController.h into
// authforge::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

class AdminController : public ::drogon::HttpController<AdminController, false>
{
  public:
    METHOD_LIST_BEGIN
    // Dashboard (merged from old AdminController)
    ADD_METHOD_TO(
      AdminController::dashboard,
      "/api/admin/dashboard",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    // Client Management routes moved to ClientAdminController (M5 Task 29a).

    // User Management routes moved to UserAdminController (M5 Task 29a).

    // Role + Scope management routes moved to RoleScopeAdminController
    // (M5 Task 29a).

    // Audit Logs
    ADD_METHOD_TO(
      AdminController::listLogs,
      "/api/admin/logs",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );

    // Token Management routes (+ OIDC keys) moved to TokenAdminController
    // (M5 Task 29a).

    // Role + Scope management routes moved to RoleScopeAdminController
    // (M5 Task 29a).

    // Dashboard Stats
    ADD_METHOD_TO(
      AdminController::getDashboardStats,
      "/api/admin/dashboard/stats",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );

    // OIDC Key Info route moved to TokenAdminController (M5 Task 29a).
    METHOD_LIST_END

    // Client Management handlers moved to ClientAdminController (M5 Task 29a).
    // User Management handlers moved to UserAdminController (M5 Task 29a).
    // Role + Scope handlers moved to RoleScopeAdminController (M5 Task 29a).

    void listLogs(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    // Token Management + OIDC keys handlers moved to TokenAdminController
    // (M5 Task 29a).

    // User Detail & Management handlers moved to UserAdminController
    // (M5 Task 29a).

    // Dashboard Stats
    void getDashboardStats(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void dashboard(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace authforge::drogon::controllers
