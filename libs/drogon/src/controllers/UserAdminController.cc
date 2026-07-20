#include <authforge/drogon/controllers/UserAdminController.h>
#include <authforge/drogon/admin/UserAdminService.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>

#include <memory>

// M5 Task 29b batch 5 (authforge-sdk-refactor): inline raw-SQL DB access from
// the Task 29a verbatim move is now delegated to UserAdminService (Mapper +
// Criteria, per .claude/rules/db-operations.md). The getUser 3-table JOIN +
// json_agg and getUserRoles JOIN are split into multiple Mapper queries
// (JOIN-in-one-query forbidden). Controller is now a thin HTTP adapter.

namespace authforge::drogon::controllers
{

namespace
{
struct UserAdminControllerDocs
{
    UserAdminControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo listUsers;
        listUsers.path = "/api/admin/users";
        listUsers.method = "GET";
        listUsers.summary = "List Users";
        listUsers.description = "Get a paginated list of users.";
        listUsers.tags = {"Admin", "Users"};
        listUsers.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(listUsers);

        ::authforge::drogon::observability::openapi::EndpointInfo disableUser;
        disableUser.path = "/api/admin/users/{userId}/disable";
        disableUser.method = "PUT";
        disableUser.summary = "Disable User";
        disableUser.description = "Disable a specific user account.";
        disableUser.tags = {"Admin", "Users"};
        disableUser.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(disableUser);

        ::authforge::drogon::observability::openapi::EndpointInfo assignUserRoles;
        assignUserRoles.path = "/api/admin/users/{userId}/roles";
        assignUserRoles.method = "PUT";
        assignUserRoles.summary = "Assign User Roles";
        assignUserRoles.description = "Assign roles to a specific user.";
        assignUserRoles.tags = {"Admin", "Users"};
        assignUserRoles.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(assignUserRoles);

        ::authforge::drogon::observability::openapi::EndpointInfo getUser;
        getUser.path = "/api/admin/users/{userId}";
        getUser.method = "GET";
        getUser.summary = "Get User Detail";
        getUser.description =
          "Get detailed information about a specific user including roles and account status.";
        getUser.tags = {"Admin", "Users"};
        getUser.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(getUser);

        ::authforge::drogon::observability::openapi::EndpointInfo updateUser;
        updateUser.path = "/api/admin/users/{userId}";
        updateUser.method = "PUT";
        updateUser.summary = "Update User";
        updateUser.description = "Update user information (email, email_verified).";
        updateUser.tags = {"Admin", "Users"};
        updateUser.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(updateUser);

        ::authforge::drogon::observability::openapi::EndpointInfo enableUser;
        enableUser.path = "/api/admin/users/{userId}/enable";
        enableUser.method = "POST";
        enableUser.summary = "Enable User";
        enableUser.description = "Enable a disabled user account by resetting lockout state.";
        enableUser.tags = {"Admin", "Users"};
        enableUser.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(enableUser);

        ::authforge::drogon::observability::openapi::EndpointInfo getUserRoles;
        getUserRoles.path = "/api/admin/users/{userId}/roles";
        getUserRoles.method = "GET";
        getUserRoles.summary = "Get User Roles";
        getUserRoles.description = "Get the roles assigned to a specific user.";
        getUserRoles.tags = {"Admin", "Users"};
        getUserRoles.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(getUserRoles);
    }
} g_userAdminControllerDocs;
}  // namespace

using UserService = ::authforge::drogon::admin::UserAdminService;

void UserAdminController::listUsers(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    UserService::listUsers(req, sharedCb);
}

void UserAdminController::disableUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    UserService::disableUser(req, sharedCb, userId);
}

void UserAdminController::assignUserRoles(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    UserService::assignUserRoles(req, sharedCb, userId);
}

void UserAdminController::getUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    UserService::getUser(req, sharedCb, userId);
}

void UserAdminController::updateUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    UserService::updateUser(req, sharedCb, userId);
}

void UserAdminController::enableUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    UserService::enableUser(req, sharedCb, userId);
}

void UserAdminController::getUserRoles(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    UserService::getUserRoles(req, sharedCb, userId);
}

}  // namespace authforge::drogon::controllers
