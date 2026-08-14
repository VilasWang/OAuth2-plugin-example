#pragma once

// M5 Task 29b batch 5 (authforge-sdk-refactor): application-service extraction
// for the user-management admin domain. Raw SQL inline in UserAdminController is
// now Mapper<T> + Criteria on the ORM Users/UserRoles/Roles models (per
// .claude/rules/db-operations.md). The getUser 3-table JOIN + json_agg and the
// getUserRoles JOIN are split into multiple Mapper queries (JOIN-in-one-query
// forbidden). Behavior equivalent (Admin API tests must stay green).
//
// Lives in libs/drogon (Adapter/SDK layer, namespace authforge::drogon::admin).

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>
#include <memory>
#include <string>

namespace authforge::drogon::admin
{

/**
 * @brief Application service for user admin operations.
 */
class UserAdminService
{
  public:
    using ResponseCallback =
      std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>>;

    static void listUsers(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);
    static void createUser(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);
    static void getUser(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &userId
    );
    static void updateUser(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &userId
    );
    static void deleteUser(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &userId
    );
    static void disableUser(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &userId
    );
    static void enableUser(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &userId
    );
    static void getUserRoles(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &userId
    );
    static void assignUserRoles(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &userId
    );
};

}  // namespace authforge::drogon::admin
