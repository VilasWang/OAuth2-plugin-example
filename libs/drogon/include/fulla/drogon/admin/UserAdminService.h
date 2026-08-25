#pragma once

// M5 Task 29b batch 5 (fulla-sdk-refactor): application-service extraction
// for the user-management admin domain. Raw SQL inline in UserAdminController is
// now Mapper<T> + Criteria on the ORM Users/UserRoles/Roles models (per
// .claude/rules/db-operations.md). The getUser 3-table JOIN + json_agg and the
// getUserRoles JOIN are split into multiple Mapper queries (JOIN-in-one-query
// forbidden). Behavior equivalent (Admin API tests must stay green).
//
// Lives in libs/drogon (Adapter/SDK layer, namespace fulla::drogon::admin).

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/orm/DbClient.h>

#include <functional>
#include <memory>
#include <string>

namespace fulla::drogon::admin
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

/**
 * @brief Last-active-admin guard (#60 item 2, shared with
 * UserSelfServiceController::deleteAccount).
 *
 * Asynchronously resolves whether `targetUserId` is an "active" admin (not
 * soft-deleted, not locked) with NO other active admin in the system.
 * Three Mapper queries (JOIN-forbidden): roles by name -> user_roles by
 * role_id -> users by id In (...) with liveness filters. `onDone(false)` also
 * covers "target is not an admin / not found" (no restriction applies).
 *
 * Known accepted race (design §6.2): two concurrent last-admin operations can
 * both observe "another admin exists"; there is no locking infrastructure that
 * fits the async-callback DB rules.
 */
void isLastActiveAdmin(
  const ::drogon::orm::DbClientPtr &db,
  int32_t targetUserId,
  std::function<void(bool)> &&onDone,
  std::function<void()> &&onError
);

}  // namespace fulla::drogon::admin
