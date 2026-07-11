#pragma once

// M5 Task 29a (authforge-sdk-refactor): the role + scope management routes
// (`/api/admin/roles*` + `/api/admin/scopes*`) carved out of the former
// AdminController (design.md §5.8 / Task 29). Verbatim move -- behavior
// unchanged, Admin API tests must stay green. The raw-SQL -> ORM Mapper +
// business-logic-to-service extraction is deferred to Task 29b.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

class RoleScopeAdminController : public ::drogon::HttpController<RoleScopeAdminController, false>
{
  public:
    METHOD_LIST_BEGIN
    // Roles
    ADD_METHOD_TO(
      RoleScopeAdminController::listRoles,
      "/api/admin/roles",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      RoleScopeAdminController::createRole,
      "/api/admin/roles",
      ::drogon::Post,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      RoleScopeAdminController::updateRole,
      "/api/admin/roles/{roleId}",
      ::drogon::Put,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      RoleScopeAdminController::deleteRole,
      "/api/admin/roles/{roleId}",
      ::drogon::Delete,
      "oauth2::filters::AuthorizationFilter"
    );
    // Scopes
    ADD_METHOD_TO(
      RoleScopeAdminController::listScopes,
      "/api/admin/scopes",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      RoleScopeAdminController::createScope,
      "/api/admin/scopes",
      ::drogon::Post,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      RoleScopeAdminController::updateScope,
      "/api/admin/scopes/{scopeId}",
      ::drogon::Put,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      RoleScopeAdminController::deleteScope,
      "/api/admin/scopes/{scopeId}",
      ::drogon::Delete,
      "oauth2::filters::AuthorizationFilter"
    );
    METHOD_LIST_END

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

    void listScopes(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

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
};

}  // namespace authforge::drogon::controllers
