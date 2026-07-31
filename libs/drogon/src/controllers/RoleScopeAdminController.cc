#include <authforge/drogon/controllers/RoleScopeAdminController.h>
#include <authforge/drogon/admin/RoleScopeAdminService.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>

#include <memory>

// M5 Task 29b batch 4 (authforge-sdk-refactor): inline raw-SQL DB access from
// the Task 29a verbatim move is now delegated to RoleScopeAdminService (Mapper
// + Criteria, per .claude/rules/db-operations.md). The listRoles JOIN+GROUP-BY
// +COUNT is split into separate Mapper queries (JOIN-in-one-query forbidden).
// Controller is now a thin HTTP adapter. Behavior equivalent (Admin API tests
// must stay green).

namespace authforge::drogon::controllers
{

namespace
{
struct RoleScopeAdminControllerDocs
{
    RoleScopeAdminControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo listScopes;
        listScopes.path = "/api/admin/scopes";
        listScopes.method = "GET";
        listScopes.summary = "List Scopes";
        listScopes.description = "Get a list of all available scopes.";
        listScopes.tags = {"Admin", "Scopes"};
        listScopes.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(listScopes);

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
    }
} g_roleScopeAdminControllerDocs;
}  // namespace

using RoleScopeService = ::authforge::drogon::admin::RoleScopeAdminService;

void RoleScopeAdminController::listRoles(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    RoleScopeService::listRoles(req, sharedCb);
}

void RoleScopeAdminController::createRole(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    RoleScopeService::createRole(req, sharedCb);
}

void RoleScopeAdminController::updateRole(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &roleId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    RoleScopeService::updateRole(req, sharedCb, roleId);
}

void RoleScopeAdminController::deleteRole(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &roleId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    RoleScopeService::deleteRole(req, sharedCb, roleId);
}

void RoleScopeAdminController::listScopes(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    RoleScopeService::listScopes(req, sharedCb);
}

void RoleScopeAdminController::createScope(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    RoleScopeService::createScope(req, sharedCb);
}

void RoleScopeAdminController::updateScope(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &scopeId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    RoleScopeService::updateScope(req, sharedCb, scopeId);
}

void RoleScopeAdminController::deleteScope(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &scopeId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    RoleScopeService::deleteScope(req, sharedCb, scopeId);
}

}  // namespace authforge::drogon::controllers
