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

    // User Management
    ADD_METHOD_TO(
      AdminController::listUsers,
      "/api/admin/users",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::disableUser,
      "/api/admin/users/{userId}/disable",
      ::drogon::Put,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::assignUserRoles,
      "/api/admin/users/{userId}/roles",
      ::drogon::Put,
      "oauth2::filters::AuthorizationFilter"
    );

    // Scope Management
    ADD_METHOD_TO(
      AdminController::listScopes,
      "/api/admin/scopes",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );

    // Audit Logs
    ADD_METHOD_TO(
      AdminController::listLogs,
      "/api/admin/logs",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );

    // Token Management
    ADD_METHOD_TO(
      AdminController::listTokens,
      "/api/admin/tokens",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::revokeTokensByClient,
      "/api/admin/tokens/revoke-by-client",
      ::drogon::Post,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::revokeTokensByUser,
      "/api/admin/tokens/revoke-by-user",
      ::drogon::Post,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::revokeToken,
      "/api/admin/tokens/{tokenPrefix}",
      ::drogon::Delete,
      "oauth2::filters::AuthorizationFilter"
    );

    // User Detail & Management
    ADD_METHOD_TO(
      AdminController::getUser,
      "/api/admin/users/{userId}",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::updateUser,
      "/api/admin/users/{userId}",
      ::drogon::Put,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::enableUser,
      "/api/admin/users/{userId}/enable",
      ::drogon::Post,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::getUserRoles,
      "/api/admin/users/{userId}/roles",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );

    // Role Management
    ADD_METHOD_TO(
      AdminController::listRoles,
      "/api/admin/roles",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::createRole,
      "/api/admin/roles",
      ::drogon::Post,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::updateRole,
      "/api/admin/roles/{roleId}",
      ::drogon::Put,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::deleteRole,
      "/api/admin/roles/{roleId}",
      ::drogon::Delete,
      "oauth2::filters::AuthorizationFilter"
    );

    // Scope Management (CRUD)
    ADD_METHOD_TO(
      AdminController::createScope,
      "/api/admin/scopes",
      ::drogon::Post,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::updateScope,
      "/api/admin/scopes/{scopeId}",
      ::drogon::Put,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      AdminController::deleteScope,
      "/api/admin/scopes/{scopeId}",
      ::drogon::Delete,
      "oauth2::filters::AuthorizationFilter"
    );

    // Dashboard Stats
    ADD_METHOD_TO(
      AdminController::getDashboardStats,
      "/api/admin/dashboard/stats",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );

    // OIDC Key Info
    ADD_METHOD_TO(
      AdminController::getOidcKeys,
      "/api/admin/oidc/keys",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    METHOD_LIST_END

    // Client Management handlers moved to ClientAdminController (M5 Task 29a).

    void listUsers(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void disableUser(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &userId
    );

    void assignUserRoles(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &userId
    );

    void listScopes(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void listLogs(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void listTokens(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void revokeToken(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &tokenPrefix
    );

    void revokeTokensByClient(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void revokeTokensByUser(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void getOidcKeys(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    // User Detail & Management
    void getUser(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &userId
    );

    void updateUser(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &userId
    );

    void enableUser(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &userId
    );

    void getUserRoles(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &userId
    );

    // Role Management
    void listRoles(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void createRole(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void updateRole(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &roleId
    );

    void deleteRole(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &roleId
    );

    // Scope Management (CRUD)
    void createScope(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void updateScope(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &scopeId
    );

    void deleteScope(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &scopeId
    );

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
