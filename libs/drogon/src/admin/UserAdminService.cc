#include <authforge/drogon/admin/UserAdminService.h>

#include <authforge/storage/postgres/models/Users.h>
#include <authforge/storage/postgres/models/UserRoles.h>
#include <authforge/storage/postgres/models/Roles.h>
#include <authforge/drogon/error/ErrorResponder.h>
#include <authforge/drogon/utils/PasswordHasher.h>
#include <authforge/drogon/adapters/DrogonAuditSink.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <authforge/common/utils/EmailNormalizer.h>

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
    ::authforge::common::error::ErrorResponder::respond(
      req,
      [cb](const ::drogon::HttpResponsePtr &r) { (*cb)(r); },
      std::move(code),
      std::move(detailForLog)
    );
}

using namespace ::drogon::orm;
using namespace ::drogon_model::oauth2_db;

// Parsed pagination query params (page is 1-based).
struct PaginationParams
{
    int page;
    int perPage;
};

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

// Current epoch seconds (locked_until is stored as epoch seconds).
int64_t nowEpochSeconds()
{
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch()
    )
                                  .count());
}

// Parse page/per_page query params with the same clamping as AuditService.
PaginationParams parsePagination(const ::drogon::HttpRequestPtr &req)
{
    PaginationParams p{1, 50};
    try { p.page = std::stoi(req->getParameter("page")); } catch (...) {}
    try { p.perPage = std::stoi(req->getParameter("per_page")); } catch (...) {}
    if (p.perPage > 100) p.perPage = 100;
    if (p.perPage < 1) p.perPage = 50;
    if (p.page < 1) p.page = 1;
    return p;
}

// Read the validated admin actor's subject from the request attributes
// (persisted by AuthorizationFilter in B1). Falls back to empty string if the
// attribute is absent (e.g. older filter) so audit never throws.
std::string adminActorId(const ::drogon::HttpRequestPtr &req)
{
    try
    {
        return req->getAttributes()->get<std::string>("userId");
    }
    catch (...)
    {
        return "";
    }
}

// Fire-and-forget audit entry. The sink is a no-op in memory storage mode, so
// this is safe to call unconditionally.
void auditFromRequest(
  const ::drogon::HttpRequestPtr &req,
  const std::string &action,
  const std::string &outcome,
  const std::string &targetType,
  const std::string &targetId
)
{
    auto *plugin = ::drogon::app().getPlugin<::OAuth2Plugin>();
    if (!plugin)
        return;
    ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
      plugin->getAuditSink(),
      action,
      outcome,
      req,
      adminActorId(req),
      targetType,
      targetId,
      Json::Value{}
    );
}

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

// Build the final paginated JSON envelope.  userIdToRoleNames is optional
// (empty when no roles were fetched, e.g. for an empty page).
void respondUserListEnvelope(
  const UserAdminService::ResponseCallback &cb,
  const std::vector<Users> &rows,
  const std::unordered_map<int32_t, std::vector<std::string>> &userIdToRoleNames,
  int page,
  int perPage,
  size_t total,
  int totalPages
)
{
    Json::Value json;
    json["status"] = "success";
    json["page"] = page;
    json["per_page"] = perPage;
    json["total"] = static_cast<Json::UInt64>(total);
    json["total_pages"] = totalPages;
    Json::Value users(Json::arrayValue);
    for (const auto &row : rows)
    {
        Json::Value user = userRowToListJson(row);
        Json::Value rolesJson(Json::arrayValue);
        auto it = userIdToRoleNames.find(row.getValueOfId());
        if (it != userIdToRoleNames.end())
        {
            for (const auto &rn : it->second)
            {
                rolesJson.append(rn);
            }
        }
        user["roles"] = rolesJson;
        users.append(user);
    }
    json["users"] = users;
    (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
}

// Given the page rows, fan-out role names (two Mapper queries — no JOIN), then
// respond with the full envelope.  total/totalPages were already computed from
// a prior count() call.
void attachRolesAndRespond(
  const ::drogon::orm::DbClientPtr &db,
  std::vector<Users> rows,
  int page,
  int perPage,
  size_t total,
  int totalPages,
  const UserAdminService::ResponseCallback &cb,
  const ::drogon::HttpRequestPtr &req
)
{
    if (rows.empty())
    {
        respondUserListEnvelope(cb, {}, {}, page, perPage, total, totalPages);
        return;
    }
    std::vector<int32_t> userIds;
    userIds.reserve(rows.size());
    for (const auto &r : rows)
    {
        userIds.push_back(r.getValueOfId());
    }
    try
    {
        Mapper<UserRoles> urMapper(db);
        urMapper.findBy(
          Criteria(UserRoles::Cols::_user_id, CompareOperator::In, userIds),
          [cb, req, db, rows = std::move(rows), page, perPage, total, totalPages](
            const std::vector<UserRoles> &allUserRoles
          ) {
              std::unordered_map<int32_t, std::vector<int32_t>> userIdToRoleIds;
              std::set<int32_t> distinctRoleIds;
              for (const auto &ur : allUserRoles)
              {
                  userIdToRoleIds[ur.getValueOfUserId()].push_back(ur.getValueOfRoleId());
                  distinctRoleIds.insert(ur.getValueOfRoleId());
              }
              if (distinctRoleIds.empty())
              {
                  respondUserListEnvelope(cb, rows, {}, page, perPage, total, totalPages);
                  return;
              }
              std::vector<int32_t> roleIdsVec(distinctRoleIds.begin(), distinctRoleIds.end());
              try
              {
                  Mapper<Roles> rMapper(db);
                  rMapper.findBy(
                    Criteria(Roles::Cols::_id, CompareOperator::In, roleIdsVec),
                    [cb, rows = std::move(rows), userIdToRoleIds = std::move(userIdToRoleIds), page, perPage, total, totalPages](
                      const std::vector<Roles> &allRoles
                    ) {
                        std::unordered_map<int32_t, std::string> roleIdToName;
                        for (const auto &r : allRoles)
                        {
                            roleIdToName[r.getValueOfId()] = r.getValueOfName();
                        }
                        std::unordered_map<int32_t, std::vector<std::string>> userIdToRoleNames;
                        for (const auto &row : rows)
                        {
                            auto it = userIdToRoleIds.find(row.getValueOfId());
                            if (it != userIdToRoleIds.end())
                            {
                                for (int32_t rid : it->second)
                                {
                                    auto rn = roleIdToName.find(rid);
                                    if (rn != roleIdToName.end())
                                    {
                                        userIdToRoleNames[row.getValueOfId()].push_back(rn->second);
                                    }
                                }
                            }
                        }
                        respondUserListEnvelope(cb, rows, userIdToRoleNames, page, perPage, total, totalPages);
                    },
                    [req, cb](const ::drogon::orm::DrogonDbException &e) {
                        respondError(req, cb, "DB_QUERY_ERROR", std::string("Failed to fetch roles: ") + e.base().what());
                    }
                  );
              }
              catch (...)
              {
                  respondError(req, cb, "DB_QUERY_ERROR", "Failed to construct roles Mapper");
              }
          },
          [req, cb](const ::drogon::orm::DrogonDbException &e) {
              respondError(req, cb, "DB_QUERY_ERROR", std::string("Failed to fetch user roles: ") + e.base().what());
          }
        );
    }
    catch (...)
    {
        respondError(req, cb, "DB_QUERY_ERROR", "Failed to construct user-roles Mapper");
    }
}
}  // namespace

void UserAdminService::listUsers(const ::drogon::HttpRequestPtr &req, ResponseCallback cb)
{
    auto pagination = parsePagination(req);
    std::string q = req->getParameter("q");
    std::string role = req->getParameter("role");
    std::string locked = req->getParameter("locked");

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    // Build the base filter from search (q) and lock-state (locked) params.
    // locked has no column — it is derived from locked_until vs current time.
    int64_t now = nowEpochSeconds();
    // Always exclude soft-deleted users.
    Criteria baseFilter = Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull);
    if (!q.empty())
    {
        // Escape LIKE wildcards and lowercase for case-insensitive prefix match.
        std::string escaped;
        for (char c : q)
        {
            if (c == '%' || c == '_') escaped += '\\';
            escaped += c;
        }
        baseFilter = baseFilter &&
                     (Criteria(Users::Cols::_username, CompareOperator::Like, escaped + "%") ||
                      Criteria(Users::Cols::_email, CompareOperator::Like, escaped + "%"));
    }
    if (locked == "true")
    {
        baseFilter = baseFilter && Criteria(Users::Cols::_locked_until, CompareOperator::GT, now);
    }
    else if (locked == "false")
    {
        baseFilter = baseFilter && Criteria(Users::Cols::_locked_until, CompareOperator::LT, now);
    }

    // Count + paginate + fan-out roles, parameterised by the final Criteria.
    auto fetchPage = [cb, req, db, pagination](Criteria filter) {
        try
        {
            Mapper<Users> countMapper(db);
            countMapper.count(
              filter,
              [cb, req, db, pagination, filter](const size_t total) {
                  int totalPages = total > 0
                                     ? (static_cast<int>(total) + pagination.perPage - 1) / pagination.perPage
                                     : 0;
                  try
                  {
                      Mapper<Users> pageMapper(db);
                      pageMapper.paginate(pagination.page, pagination.perPage)
                        .orderBy(Users::Cols::_id, SortOrder::ASC)
                        .findBy(
                          filter,
                          [cb, req, db, pagination, total, totalPages](const std::vector<Users> &rows) {
                              attachRolesAndRespond(
                                db, std::move(rows), pagination.page, pagination.perPage, total, totalPages, cb, req
                              );
                          },
                          [req, cb](const ::drogon::orm::DrogonDbException &e) {
                              respondError(
                                req, cb, "DB_QUERY_ERROR", std::string("Failed to fetch users: ") + e.base().what()
                              );
                          }
                        );
                  }
                  catch (...)
                  {
                      respondError(req, cb, "DB_QUERY_ERROR", "Failed to construct page Mapper");
                  }
              },
              [req, cb](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req, cb, "DB_QUERY_ERROR", std::string("Failed to count users: ") + e.base().what()
                  );
              }
            );
        }
        catch (...)
        {
            respondError(req, cb, "DB_QUERY_ERROR", "Failed to construct count Mapper");
        }
    };

    if (role.empty())
    {
        fetchPage(baseFilter);
    }
    else
    {
        // JOIN-forbidden: resolve role name -> roleIds -> userIds first, then
        // intersect with the base filter via Criteria(id, In, userIds).
        try
        {
            Mapper<Roles> roleMapper(db);
            roleMapper.findBy(
              Criteria(Roles::Cols::_name, CompareOperator::EQ, role),
              [cb, req, db, baseFilter, fetchPage, pagination](const std::vector<Roles> &roles) {
                  if (roles.empty())
                  {
                      // Role does not exist → empty result.
                      respondUserListEnvelope(cb, {}, {}, pagination.page, pagination.perPage, 0, 0);
                      return;
                  }
                  std::vector<int32_t> roleIds;
                  roleIds.reserve(roles.size());
                  for (const auto &r : roles)
                  {
                      roleIds.push_back(r.getValueOfId());
                  }
                  try
                  {
                      Mapper<UserRoles> urMapper(db);
                      urMapper.findBy(
                        Criteria(UserRoles::Cols::_role_id, CompareOperator::In, roleIds),
                        [cb, req, db, baseFilter, fetchPage, pagination](
                          const std::vector<UserRoles> &userRoles
                        ) {
                            if (userRoles.empty())
                            {
                                respondUserListEnvelope(cb, {}, {}, pagination.page, pagination.perPage, 0, 0);
                                return;
                            }
                            std::set<int32_t> userIdSet;
                            for (const auto &ur : userRoles)
                            {
                                userIdSet.insert(ur.getValueOfUserId());
                            }
                            std::vector<int32_t> userIds(userIdSet.begin(), userIdSet.end());
                            Criteria finalFilter =
                              baseFilter && Criteria(Users::Cols::_id, CompareOperator::In, userIds);
                            fetchPage(finalFilter);
                        },
                        [req, cb](const ::drogon::orm::DrogonDbException &e) {
                            respondError(
                              req, cb, "DB_QUERY_ERROR",
                              std::string("Failed to fetch user-roles for role filter: ") + e.base().what()
                            );
                        }
                      );
                  }
                  catch (...)
                  {
                      respondError(req, cb, "DB_QUERY_ERROR", "Failed to construct user-roles Mapper");
                  }
              },
              [req, cb](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req, cb, "DB_QUERY_ERROR",
                    std::string("Failed to fetch role for filter: ") + e.base().what()
                  );
              }
            );
        }
        catch (...)
        {
            respondError(req, cb, "DB_QUERY_ERROR", "Failed to construct role Mapper");
        }
    }
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

void UserAdminService::createUser(const ::drogon::HttpRequestPtr &req, ResponseCallback cb)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(req, cb, "VALIDATION_INVALID_INPUT", "Invalid JSON body");
        return;
    }
    std::string username = (*jsonBody).get("username", "").asString();
    std::string password = (*jsonBody).get("password", "").asString();
    std::string email = (*jsonBody).get("email", "").asString();
    bool emailVerified = (*jsonBody).get("email_verified", false).asBool();
    bool mfaEnabled = (*jsonBody).get("mfa_enabled", false).asBool();

    if (username.empty())
    {
        respondError(req, cb, "VALIDATION_MISSING_REQUIRED_FIELD", "username is required");
        return;
    }
    if (password.empty())
    {
        respondError(req, cb, "VALIDATION_MISSING_REQUIRED_FIELD", "password is required");
        return;
    }

    // Hash password with PBKDF2-SHA256 (salt embedded in the hash string, so
    // the Users.salt column is set to "" — same convention as AuthService).
    std::string passwordHash;
    try
    {
        passwordHash = ::authforge::common::utils::PasswordHasher::hash(password);
    }
    catch (const std::exception &e)
    {
        respondError(req, cb, "INTERNAL_ERROR", std::string("Password hashing failed: ") + e.what());
        return;
    }

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    Users row;
    row.setUsername(username);
    row.setPasswordHash(passwordHash);
    row.setSalt("");
    if (!email.empty())
    {
        row.setEmail(::authforge::common::utils::normalizeEmail(email));
    }
    row.setEmailVerified(emailVerified);
    row.setMfaEnabled(mfaEnabled);
    // org_id / roles handled in follow-up if specified.
    if (jsonBody->isMember("org_id") && (*jsonBody)["org_id"].isInt())
    {
        row.setOrgId((*jsonBody)["org_id"].asInt());
    }

    try
    {
        Mapper<Users> mapper(db);
        mapper.insert(
          row,
          [cb, req, db, username](const Users &inserted) {
              int32_t newId = inserted.getValueOfId();

              // Resolve default "user" role (or body.roles if provided) and
              // assign it to the new user.  If the role resolution / insert
              // fails we still report success for the user creation — the role
              // can be assigned later via PUT /roles.
              auto assignDefaultRole = [cb, req, newId, username, inserted]() {
                  auditFromRequest(req, "user_create", "success", "user", std::to_string(newId));
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "User created successfully";
                  json["user"] = userRowToListJson(inserted);
                  json["user"]["created_at"] = inserted.getValueOfCreatedAt().toDbString();
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                  resp->setStatusCode(::drogon::k201Created);
                  (*cb)(resp);
              };

              // Determine which roles to assign.
              std::vector<std::string> roleNames;
              if (req->getJsonObject() && req->getJsonObject()->isMember("roles") &&
                  (*req->getJsonObject())["roles"].isArray())
              {
                  for (const auto &r : (*req->getJsonObject())["roles"])
                  {
                      if (r.isString())
                          roleNames.push_back(r.asString());
                  }
              }
              if (roleNames.empty())
              {
                  roleNames.push_back("user");  // default role
              }

              // Resolve role names → ids, then insert user_roles rows.
              try
              {
                  Mapper<Roles> rMapper(db);
                  rMapper.findBy(
                    Criteria(Roles::Cols::_name, CompareOperator::In, roleNames),
                    [cb, req, db, newId, assignDefaultRole](const std::vector<Roles> &resolved) {
                        // Insert each role assignment; fire response when all
                        // complete (or immediately if none resolved).
                        if (resolved.empty())
                        {
                            assignDefaultRole();
                            return;
                        }
                        auto remaining = std::make_shared<std::atomic<int>>(static_cast<int>(resolved.size()));
                        for (const auto &r : resolved)
                        {
                            UserRoles ur;
                            ur.setUserId(newId);
                            ur.setRoleId(r.getValueOfId());
                            try
                            {
                                Mapper<UserRoles> insMapper(db);
                                insMapper.insert(
                                  ur,
                                  [remaining, assignDefaultRole](const UserRoles &) {
                                      if (remaining->fetch_sub(1) == 1)
                                          assignDefaultRole();
                                  },
                                  [remaining, assignDefaultRole](const ::drogon::orm::DrogonDbException &) {
                                      if (remaining->fetch_sub(1) == 1)
                                          assignDefaultRole();
                                  }
                                );
                            }
                            catch (...)
                            {
                                if (remaining->fetch_sub(1) == 1)
                                    assignDefaultRole();
                            }
                        }
                    },
                    [req, assignDefaultRole](const ::drogon::orm::DrogonDbException &) {
                        // Role lookup failed — still report user creation success.
                        assignDefaultRole();
                    }
                  );
              }
              catch (...)
              {
                  assignDefaultRole();
              }
          },
          [req, cb](const ::drogon::orm::DrogonDbException &e) {
              // Distinguish username vs email UNIQUE violations by constraint
              // name (same pattern as AuthService / PostgresIdentityRepository).
              std::string what = e.base().what();
              if (what.find("users_username_key") != std::string::npos)
              {
                  respondError(req, cb, "VALIDATION_USERNAME_TAKEN", "Username already exists");
              }
              else if (what.find("idx_users_email_unique") != std::string::npos)
              {
                  respondError(req, cb, "VALIDATION_EMAIL_TAKEN", "Email already in use");
              }
              else
              {
                  respondError(req, cb, "DB_QUERY_ERROR", std::string("Failed to create user: ") + what);
              }
          }
        );
    }
    catch (...)
    {
        respondError(req, cb, "DB_QUERY_ERROR", "Failed to construct insert Mapper");
    }
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
      Criteria(Users::Cols::_id, CompareOperator::EQ, id) &&
        Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull),
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
          int64_t now = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                               std::chrono::system_clock::now().time_since_epoch()
          )
                                               .count());
          json["locked"] = (lockedUntil > now);
          json["locked_until"] = lockedUntil;
          json["org_id"] = row.getValueOfOrgId();
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
    bool hasUsername = jsonBody->isMember("username");
    bool hasMfaEnabled = jsonBody->isMember("mfa_enabled");
    bool hasLocked = jsonBody->isMember("locked");
    bool hasOrgId = jsonBody->isMember("org_id");
    if (!hasEmail && !hasEmailVerified && !hasUsername && !hasMfaEnabled && !hasLocked && !hasOrgId)
    {
        respondError(req, cb, "VALIDATION_INVALID_INPUT", "No updatable fields provided");
        return;
    }

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    try
    {
        Mapper<Users> mapper(db);
        mapper.findOne(
          Criteria(Users::Cols::_id, CompareOperator::EQ, id) &&
        Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull),
          [cb, req, jsonBody, hasEmail, hasEmailVerified, hasUsername, hasMfaEnabled, hasLocked, hasOrgId, db, id](
            Users row
          ) {
              if (hasEmail)
              {
                  row.setEmail(::authforge::common::utils::normalizeEmail((*jsonBody)["email"].asString()));
              }
              if (hasEmailVerified)
              {
                  row.setEmailVerified((*jsonBody)["email_verified"].asBool());
              }
              if (hasUsername)
              {
                  row.setUsername((*jsonBody)["username"].asString());
              }
              if (hasMfaEnabled && (*jsonBody)["mfa_enabled"].isBool())
              {
                  row.setMfaEnabled((*jsonBody)["mfa_enabled"].asBool());
              }
              if (hasLocked && (*jsonBody)["locked"].isBool())
              {
                  // locked is derived from locked_until: true → forever sentinel,
                  // false → 0 (unlocked, matching enableUser).
                  row.setLockedUntil((*jsonBody)["locked"].asBool() ? kLockedForeverSentinel : 0);
              }
              if (hasOrgId && (*jsonBody)["org_id"].isInt())
              {
                  row.setOrgId((*jsonBody)["org_id"].asInt());
              }
              try
              {
                  Mapper<Users> updateMapper(db);
                  updateMapper.update(
                    row,
                    [cb, req, id](const size_t) {
                        auditFromRequest(req, "user_update", "success", "user", std::to_string(id));
                        Json::Value json;
                        json["status"] = "success";
                        json["message"] = "User updated successfully";
                        (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                    },
                    [req, cb](const ::drogon::orm::DrogonDbException &e) {
                        // Distinguish username vs email UNIQUE violations.
                        std::string what = e.base().what();
                        if (what.find("users_username_key") != std::string::npos)
                        {
                            respondError(req, cb, "VALIDATION_USERNAME_TAKEN", "Username already exists");
                        }
                        else if (what.find("idx_users_email_unique") != std::string::npos)
                        {
                            respondError(req, cb, "VALIDATION_EMAIL_TAKEN", "Email already in use");
                        }
                        else
                        {
                            respondError(
                              req, cb, "DB_QUERY_ERROR",
                              std::string("Failed to update user: ") + what
                            );
                        }
                    }
                  );
              }
              catch (...)
              {
                  respondError(req, cb, "DB_QUERY_ERROR", "Failed to construct update Mapper");
              }
          },
          [req, cb](const ::drogon::orm::DrogonDbException &) {
              respondError(req, cb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found");
          }
        );
    }
    catch (...)
    {
        respondError(req, cb, "DB_QUERY_ERROR", "Failed to construct findOne Mapper");
    }
}

void UserAdminService::deleteUser(
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

    // Soft-delete: set deleted_at to current timestamp. The findOne already
    // excludes deleted users (deleted_at IS NULL filter), so a 404 is returned
    // for already-deleted users.
    try
    {
        Mapper<Users> mapper(db);
        mapper.findOne(
          Criteria(Users::Cols::_id, CompareOperator::EQ, id) &&
            Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull),
          [cb, req, db, id](Users row) {
              // Self-delete guard: prevent an admin from deleting their own account.
              if (row.getValueOfPublicSub() == adminActorId(req))
              {
                  respondError(req, cb, "VALIDATION_INVALID_INPUT", "Cannot delete your own account");
                  return;
              }
              row.setDeletedAt(::trantor::Date::now());
              std::string publicSub = row.getValueOfPublicSub();
              try
              {
                  Mapper<Users> updateMapper(db);
                  updateMapper.update(
                    row,
                    [cb, req, id, db, publicSub](const size_t) {
                        auditFromRequest(req, "user_delete", "success", "user", std::to_string(id));
                        // Revoke all outstanding tokens — introspection and
                        // refresh copy the subject from stored token rows and
                        // never query users, so existing tokens would otherwise
                        // stay valid until natural expiry.
                        try
                        {
                            db->execSqlAsync(
                              "UPDATE oauth2_access_tokens SET revoked = true WHERE user_id = $1",
                              [](const ::drogon::orm::Result &) {},
                              [](const ::drogon::orm::DrogonDbException &) {},
                              publicSub
                            );
                            db->execSqlAsync(
                              "UPDATE oauth2_refresh_tokens SET revoked = true WHERE user_id = $1",
                              [](const ::drogon::orm::Result &) {},
                              [](const ::drogon::orm::DrogonDbException &) {},
                              publicSub
                            );
                        }
                        catch (...)
                        {
                        }
                        Json::Value json;
                        json["status"] = "success";
                        json["message"] = "User deleted successfully";
                        json["user_id"] = id;
                        (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                    },
                    [req, cb](const ::drogon::orm::DrogonDbException &e) {
                        respondError(
                          req, cb, "DB_QUERY_ERROR",
                          std::string("Failed to delete user: ") + e.base().what()
                        );
                    }
                  );
              }
              catch (...)
              {
                  respondError(req, cb, "DB_QUERY_ERROR", "Failed to construct update Mapper");
              }
          },
          [req, cb](const ::drogon::orm::DrogonDbException &) {
              respondError(req, cb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found");
          }
        );
    }
    catch (...)
    {
        respondError(req, cb, "DB_QUERY_ERROR", "Failed to construct findOne Mapper");
    }
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
      Criteria(Users::Cols::_id, CompareOperator::EQ, id) &&
        Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull),
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
                  req,
                  cb,
                  "DB_QUERY_ERROR",
                  std::string("Failed to disable user: ") + e.base().what()
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
      Criteria(Users::Cols::_id, CompareOperator::EQ, id) &&
        Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull),
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
                  req,
                  cb,
                  "DB_QUERY_ERROR",
                  std::string("Failed to enable user: ") + e.base().what()
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
                std::sort(sorted.begin(), sorted.end(), [](const Roles &a, const Roles &b) {
                    return a.getValueOfName() < b.getValueOfName();
                });
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
            req,
            cb,
            "DB_QUERY_ERROR",
            std::string("Failed to clear existing roles: ") + e.base().what()
          );
      }
    );
}

}  // namespace authforge::drogon::admin
