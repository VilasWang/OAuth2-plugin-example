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
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::createUser,
      "/api/admin/users",
      ::drogon::Post,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::getUser,
      "/api/admin/users/{userId}",
      ::drogon::Get,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::updateUser,
      "/api/admin/users/{userId}",
      ::drogon::Put,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::deleteUser,
      "/api/admin/users/{userId}",
      ::drogon::Delete,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::disableUser,
      "/api/admin/users/{userId}/disable",
      ::drogon::Put,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::enableUser,
      "/api/admin/users/{userId}/enable",
      ::drogon::Post,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::getUserRoles,
      "/api/admin/users/{userId}/roles",
      ::drogon::Get,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      UserAdminController::assignUserRoles,
      "/api/admin/users/{userId}/roles",
      ::drogon::Put,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    METHOD_LIST_END

    void listUsers(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void createUser(
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

    void deleteUser(
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

    // #43 resource-scope authorization: explicit, order-independent endpoint
    // + scope-requirement registration (replaces the former file-scope
    // static-init struct, defect 1.1 SIOF). Called from main()/test_main()
    // alongside the other controllers' initApiDocs().
    static void initApiDocs();

  private:
    static void initApiDocsImpl();
};

}  // namespace authforge::drogon::controllers
