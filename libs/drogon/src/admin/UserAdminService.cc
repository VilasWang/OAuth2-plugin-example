#include <authforge/drogon/admin/UserAdminService.h>

#include <authforge/storage/postgres/models/Users.h>
#include <authforge/storage/postgres/models/UserRoles.h>
#include <authforge/storage/postgres/models/Roles.h>
#include <oauth2/error/ErrorResponder.h>
#include <oauth2/utils/EmailNormalizer.h>

#include <drogon/drogon.h>
#include <trantor/utils/Date.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace authforge::drogon::admin
{

namespace
{
void respondError(
  const ::drogon::HttpRequestPtr &req,
  const UserAdminService::ResponseCallback &cb,
  std::string code,
  std::string detailForLog = ""
)
{
    ::common::error::ErrorResponder::respond(
      req,
      [cb](const ::drogon::HttpResponsePtr &r) { (*cb)(r); },
      std::move(code),
      std::move(detailForLog)
    );
}

using namespace ::drogon::orm;
using namespace ::drogon_model::oauth2_db;

::drogon::orm::DbClientPtr getDbOrRespond(
  const ::drogon::HttpRequestPtr &req,
  const UserAdminService::ResponseCallback &cb
)
{
    try
    {
        return ::drogon::app().getDbClient();
    }
    catch (...)
    {
        respondError(req, cb, "DB_CONNECTION_ERROR", "Database unavailable");
        return nullptr;
    }
}

// Sentinel epoch (year ~2286) the original disableUser used to mean "locked
// forever". Kept identical so lockedUntil semantics are unchanged.
constexpr int64_t kLockedForeverSentinel = 9999999999;

Json::Value userRowToListJson(const Users &row)
{
    Json::Value user;
    user["id"] = row.getValueOfId();
    user["username"] = row.getValueOfUsername();
    user["email"] = row.getValueOfEmail();
    user["email_verified"] = row.getValueOfEmailVerified();
    user["mfa_enabled"] = row.getValueOfMfaEnabled();
    return user;
}
}  // namespace

void UserAdminService::listUsers(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback cb
)
{
    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    Mapper<Users> mapper(db);
    mapper.findBy(
      Criteria(),
      [cb](const std::vector<Users> &rows) {
          Json::Value json;
          json["status"] = "success";
          Json::Value users(Json::arrayValue);
          for (const auto &row : rows)
          {
              users.append(userRowToListJson(row));
          }
          json["users"] = users;
          json["total"] = static_cast<int>(rows.size());
          (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
      },
      [req, cb](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, cb, "DB_QUERY_ERROR", std::string("Failed to fetch users: ") + e.base().what()
          );
      }
    );
}

// Fetch the role NAMES assigned to a user (used by getUser + getUserRoles).
// Two Mapper queries (no JOIN): user_roles by user_id -> role_ids, then roles
// by id IN (...). Returned in role.name ascending order to match the original
// getUserRoles ORDER BY r.name.
void fetchUserRoleNames(
  const ::drogon::orm::DbClientPtr &db,
  int32_t userId,
  std::function<void(std::vector<std::pair<int32_t, std::string>>)> &&onDone,
  const ::drogon::HttpRequestPtr &req,
  const UserAdminService::ResponseCallback &cb,
  const char *errCtx
)
{
    Mapper<UserRoles> urMapper(db);
    urMapper.findBy(
      Criteria(UserRoles::Cols::_user_id, CompareOperator::EQ, userId),
      [db, onDone = std::move(onDone), req, cb, errCtx](const std::vector<UserRoles> &userRoles) {
          if (userRoles.empty())
          {
              onDone({});
              return;
          }
          std::vector<int32_t> roleIds;
          roleIds.reserve(userRoles.size());
          for (const auto &ur : userRoles)
          {
              roleIds.push_back(ur.getValueOfRoleId());
          }
          Mapper<Roles> rMapper(db);
          rMapper.findBy(
            Criteria(Roles::Cols::_id, CompareOperator::In, roleIds),
            [onDone = std::move(onDone)](const std::vector<Roles> &roles) {
                std::vector<std::pair<int32_t, std::string>> out;
                out.reserve(roles.size());
                for (const auto &r : roles)
                {
                    out.emplace_back(r.getValueOfId(), r.getValueOfName());
                }
                std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
                    return a.second < b.second;
                });
                onDone(std::move(out));
            },
            [req, cb, errCtx](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req, cb, "DB_QUERY_ERROR", std::string(errCtx) + ": " + e.base().what()
                );
            }
          );
      },
      [req, cb, errCtx](const ::drogon::orm::DrogonDbException &e) {
          respondError(req, cb, "DB_QUERY_ERROR", std::string(errCtx) + ": " + e.base().what());
      }
    );
}

void UserAdminService::getUser(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback cb,
  const std::string &userId
)
{
    int32_t id = 0;
    try
    {
        id = std::stoi(userId);
    }
    catch (...)
    {
        respondError(req, cb, "VALIDATION_INVALID_INPUT", "userId must be an integer");
        return;
    }

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    // Original: a 3-table JOIN (users LEFT JOIN user_roles LEFT JOIN roles) with
    // json_agg. Split: (1) fetch the user, (2) fetch role names via
    // fetchUserRoleNames (itself two queries). Aggregation in-memory.
    Mapper<Users> mapper(db);
    mapper.findOne(
      Criteria(Users::Cols::_id, CompareOperator::EQ, id),
      [cb, req, db, id](const Users &row) {
          Json::Value json;
          json["status"] = "success";
          json["id"] = row.getValueOfId();
          json["username"] = row.getValueOfUsername();
          json["email"] = row.getValueOfEmail();
          json["email_verified"] = row.getValueOfEmailVerified();
          json["mfa_enabled"] = row.getValueOfMfaEnabled();
          json["failed_login_count"] = row.getValueOfFailedLoginCount();
          int64_t lockedUntil = row.getValueOfLockedUntil();
          int64_t now = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch()
            ).count()
          );
          json["locked"] = (lockedUntil > now);
          json["locked_until"] = lockedUntil;
          json["created_at"] = row.getValueOfCreatedAt().toDbString();

          fetchUserRoleNames(
            db,
            id,
            [json, cb](std::vector<std::pair<int32_t, std::string>> roleNames) {
                Json::Value rolesJson(Json::arrayValue);
                for (const auto &rn : roleNames)
                {
                    rolesJson.append(rn.second);
                }
                Json::Value resp = json;
                resp["roles"] = rolesJson;
                (*cb)(::drogon::HttpResponse::newHttpJsonResponse(resp));
            },
            req,
            cb,
            "Failed to fetch user"
          );
      },
      [req, cb](const ::drogon::orm::DrogonDbException &) {
          respondError(req, cb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found");
      }
    );
}

void UserAdminService::updateUser(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback cb,
  const std::string &userId
)
{
    int32_t id = 0;
    try
    {
        id = std::stoi(userId);
    }
    catch (...)
    {
        respondError(req, cb, "VALIDATION_INVALID_INPUT", "userId must be an integer");
        return;
    }

    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(req, cb, "VALIDATION_INVALID_INPUT", "Invalid JSON body");
        return;
    }
    bool hasEmail = jsonBody->isMember("email");
    bool hasEmailVerified = jsonBody->isMember("email_verified");
    if (!hasEmail && !hasEmailVerified)
    {
        respondError(req, cb, "VALIDATION_INVALID_INPUT", "No updatable fields provided");
        return;
    }

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    Mapper<Users> mapper(db);
    mapper.findOne(
      Criteria(Users::Cols::_id, CompareOperator::EQ, id),
      [cb, req, jsonBody, hasEmail, hasEmailVerified, db](Users row) {
          if (hasEmail)
          {
              // Normalize on write (matches original -- login/password-reset
              // lookups use the canonical form).
              row.setEmail(::oauth2::utils::normalizeEmail((*jsonBody)["email"].asString()));
          }
          if (hasEmailVerified)
          {
              row.setEmailVerified((*jsonBody)["email_verified"].asBool());
          }
          Mapper<Users> updateMapper(db);
          updateMapper.update(
            row,
            [cb, req](const size_t) {
                Json::Value json;
                json["status"] = "success";
                json["message"] = "User updated successfully";
                (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
            },
            [req, cb](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req, cb, "DB_QUERY_ERROR", std::string("Failed to update user: ") + e.base().what()
                );
            }
          );
      },
      [req, cb](const ::drogon::orm::DrogonDbException &) {
          respondError(req, cb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found");
      }
    );
}

void UserAdminService::disableUser(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback cb,
  const std::string &userId
)
{
    int32_t id = 0;
    try
    {
        id = std::stoi(userId);
    }
    catch (...)
    {
        respondError(req, cb, "VALIDATION_INVALID_INPUT", "userId must be an integer");
        return;
    }

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    Mapper<Users> mapper(db);
    mapper.findOne(
      Criteria(Users::Cols::_id, CompareOperator::EQ, id),
      [cb, req, userId, db](Users row) {
          row.setLockedUntil(kLockedForeverSentinel);
          Mapper<Users> updateMapper(db);
          updateMapper.update(
            row,
            [cb, userId](const size_t) {
                Json::Value json;
                json["status"] = "success";
                json["message"] = "User disabled successfully";
                json["user_id"] = userId;
                (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
            },
            [req, cb](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req, cb, "DB_QUERY_ERROR", std::string("Failed to disable user: ") + e.base().what()
                );
            }
          );
      },
      [req, cb](const ::drogon::orm::DrogonDbException &) {
          respondError(req, cb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found");
      }
    );
}

void UserAdminService::enableUser(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback cb,
  const std::string &userId
)
{
    int32_t id = 0;
    try
    {
        id = std::stoi(userId);
    }
    catch (...)
    {
        respondError(req, cb, "VALIDATION_INVALID_INPUT", "userId must be an integer");
        return;
    }

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    Mapper<Users> mapper(db);
    mapper.findOne(
      Criteria(Users::Cols::_id, CompareOperator::EQ, id),
      [cb, req, userId, db](Users row) {
          row.setLockedUntil(0);
          row.setFailedLoginCount(0);
          Mapper<Users> updateMapper(db);
          updateMapper.update(
            row,
            [cb, userId](const size_t) {
                Json::Value json;
                json["status"] = "success";
                json["message"] = "User enabled successfully";
                json["user_id"] = userId;
                (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
            },
            [req, cb](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req, cb, "DB_QUERY_ERROR", std::string("Failed to enable user: ") + e.base().what()
                );
            }
          );
      },
      [req, cb](const ::drogon::orm::DrogonDbException &) {
          respondError(req, cb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found");
      }
    );
}

void UserAdminService::getUserRoles(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback cb,
  const std::string &userId
)
{
    int32_t id = 0;
    try
    {
        id = std::stoi(userId);
    }
    catch (...)
    {
        respondError(req, cb, "VALIDATION_INVALID_INPUT", "userId must be an integer");
        return;
    }

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    // Original: SELECT r.* FROM roles JOIN user_roles ... WHERE user_id.
    // Reuse fetchUserRoleNames but also surface description (the original
    // response includes id/name/description). Fetch roles directly via the
    // two-query split: user_roles -> role_ids -> roles IN (...).
    Mapper<UserRoles> urMapper(db);
    urMapper.findBy(
      Criteria(UserRoles::Cols::_user_id, CompareOperator::EQ, id),
      [cb, req, db](const std::vector<UserRoles> &userRoles) {
          if (userRoles.empty())
          {
              Json::Value json;
              json["status"] = "success";
              json["roles"] = Json::Value(Json::arrayValue);
              (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              return;
          }
          std::vector<int32_t> roleIds;
          roleIds.reserve(userRoles.size());
          for (const auto &ur : userRoles)
          {
              roleIds.push_back(ur.getValueOfRoleId());
          }
          Mapper<Roles> rMapper(db);
          rMapper.findBy(
            Criteria(Roles::Cols::_id, CompareOperator::In, roleIds),
            [cb](const std::vector<Roles> &roles) {
                std::vector<Roles> sorted = roles;
                std::sort(
                  sorted.begin(),
                  sorted.end(),
                  [](const Roles &a, const Roles &b) {
                      return a.getValueOfName() < b.getValueOfName();
                  }
                );
                Json::Value json;
                json["status"] = "success";
                Json::Value rolesJson(Json::arrayValue);
                for (const auto &r : sorted)
                {
                    Json::Value role;
                    role["id"] = r.getValueOfId();
                    role["name"] = r.getValueOfName();
                    role["description"] = r.getValueOfDescription();
                    rolesJson.append(role);
                }
                json["roles"] = rolesJson;
                (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
            },
            [req, cb](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req,
                  cb,
                  "DB_QUERY_ERROR",
                  std::string("Failed to fetch user roles: ") + e.base().what()
                );
            }
          );
      },
      [req, cb](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, cb, "DB_QUERY_ERROR", std::string("Failed to fetch user roles: ") + e.base().what()
          );
      }
    );
}

void UserAdminService::assignUserRoles(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback cb,
  const std::string &userId
)
{
    int32_t id = 0;
    try
    {
        id = std::stoi(userId);
    }
    catch (...)
    {
        respondError(req, cb, "VALIDATION_INVALID_INPUT", "userId must be an integer");
        return;
    }

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("roles") || !(*jsonBody)["roles"].isArray())
    {
        respondError(
          req, cb, "VALIDATION_MISSING_REQUIRED_FIELD", "Request body must contain a 'roles' array"
        );
        return;
    }

    std::vector<std::string> roleNames;
    for (const auto &role : (*jsonBody)["roles"])
    {
        if (role.isString())
        {
            roleNames.push_back(role.asString());
        }
    }

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    // Step 1: clear existing roles for the user (deleteBy). Step 2: resolve
    // each requested role name to its id, then insert user_roles. The original
    // used `INSERT INTO user_roles SELECT $1, id FROM roles WHERE name=$2`
    // (INSERT...SELECT) -- a documented batch pattern, but Mapper::insert can't
    // express it, so resolve names -> ids first (one findBy(In names)) then
    // insert each assignment.
    Mapper<UserRoles> urMapper(db);
    urMapper.deleteBy(
      Criteria(UserRoles::Cols::_user_id, CompareOperator::EQ, id),
      [cb, req, db, id, roleNames](const size_t) {
          if (roleNames.empty())
          {
              Json::Value json;
              json["status"] = "success";
              json["message"] = "User roles updated successfully";
              json["user_id"] = id;
              json["roles"] = Json::Value(Json::arrayValue);
              (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              return;
          }

          // Resolve role names -> role rows (single findBy In).
          Mapper<Roles> rMapper(db);
          rMapper.findBy(
            Criteria(Roles::Cols::_name, CompareOperator::In, roleNames),
            [cb, req, db, id, roleNames](const std::vector<Roles> &resolved) {
                if (resolved.empty())
                {
                    // No requested names matched any role -- nothing inserted;
                    // report success with empty assigned set (preserves
                    // original "skip unknown role" behavior).
                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "User roles updated successfully";
                    json["user_id"] = id;
                    json["roles"] = Json::Value(Json::arrayValue);
                    (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                    return;
                }
                // name -> id map for ordering the response by request order.
                std::unordered_map<std::string, int32_t> nameToId;
                for (const auto &r : resolved)
                {
                    nameToId[r.getValueOfName()] = r.getValueOfId();
                }
                auto remaining =
                  std::make_shared<std::atomic<int>>(static_cast<int>(resolved.size()));
                auto assigned = std::make_shared<std::vector<std::string>>();
                auto mu = std::make_shared<std::mutex>();

                for (const auto &r : resolved)
                {
                    UserRoles row;
                    row.setUserId(id);
                    row.setRoleId(r.getValueOfId());
                    Mapper<UserRoles> insMapper(db);
                    insMapper.insert(
                      row,
                      [cb, req, remaining, assigned, mu, name = r.getValueOfName(), id](
                        const UserRoles &
                      ) {
                          {
                              std::lock_guard<std::mutex> lock(*mu);
                              assigned->push_back(name);
                          }
                          if (remaining->fetch_sub(1) == 1)
                          {
                              Json::Value json;
                              json["status"] = "success";
                              json["message"] = "User roles updated successfully";
                              json["user_id"] = id;
                              Json::Value rolesJson(Json::arrayValue);
                              {
                                  std::lock_guard<std::mutex> lock(*mu);
                                  for (const auto &n : *assigned)
                                  {
                                      rolesJson.append(n);
                                  }
                              }
                              json["roles"] = rolesJson;
                              (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                          }
                      },
                      [cb, req, remaining](const ::drogon::orm::DrogonDbException &e) {
                          if (remaining->fetch_sub(1) == 1)
                          {
                              respondError(
                                req,
                                cb,
                                "DB_QUERY_ERROR",
                                std::string("Failed to assign some roles: ") + e.base().what()
                              );
                          }
                      }
                    );
                }
            },
            [req, cb](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req,
                  cb,
                  "DB_QUERY_ERROR",
                  std::string("Failed to resolve role names: ") + e.base().what()
                );
            }
          );
      },
      [req, cb](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, cb, "DB_QUERY_ERROR", std::string("Failed to clear existing roles: ") + e.base().what()
          );
      }
    );
}

}  // namespace authforge::drogon::admin
