#pragma once

// M5 Task 29a (authforge-sdk-refactor): the user-management routes
// (`/api/admin/users*`) carved out of the former AdminController (design.md
// §5.8 / Task 29). Verbatim move -- behavior unchanged, Admin API tests must
// stay green. The raw-SQL -> ORM Mapper + business-logic-to-service extraction
// is deferred to Task 29b.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

class UserAdminController : public ::drogon::HttpController<UserAdminController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(
      UserAdminController::listUsers,
      "/api/admin/users",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::getUser,
      "/api/admin/users/{userId}",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::updateUser,
      "/api/admin/users/{userId}",
      ::drogon::Put,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::disableUser,
      "/api/admin/users/{userId}/disable",
      ::drogon::Put,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::enableUser,
      "/api/admin/users/{userId}/enable",
      ::drogon::Post,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::getUserRoles,
      "/api/admin/users/{userId}/roles",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::assignUserRoles,
      "/api/admin/users/{userId}/roles",
      ::drogon::Put,
      "oauth2::filters::AuthorizationFilter"
    );
    METHOD_LIST_END

    void listUsers(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

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

    void disableUser(
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

    void assignUserRoles(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &userId
    );
};

}  // namespace authforge::drogon::controllers
