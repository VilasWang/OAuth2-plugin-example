#include <authforge/drogon/controllers/UserAdminController.h>
#include <drogon/drogon.h>
#include <oauth2/utils/EmailNormalizer.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

// M5 Task 29a (authforge-sdk-refactor): user-management routes moved verbatim
// from AdminController.cc. respondError helper, OpenAPI docs struct, and every
// handler body are byte-for-byte copies -- no behavior change (Admin API tests
// must stay green). The raw SQL and business logic remain inline pending
// Task 29b extraction.

namespace authforge::drogon::controllers
{

namespace
{
// Emit an Application error via the unified ErrorResponder entry point so the
// body is always an Error Envelope (Requirement 7.1 / 7.3 / 7.5).
void respondError(
  const ::drogon::HttpRequestPtr &req,
  const std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>> &cb,
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

void UserAdminController::listUsers(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT id, username, email, email_verified, mfa_enabled "
          "FROM users ORDER BY id",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              Json::Value json;
              json["status"] = "success";
              Json::Value users(Json::arrayValue);

              for (const auto &row : result)
              {
                  Json::Value user;
                  user["id"] = row["id"].as<int>();
                  user["username"] = row["username"].as<std::string>();
                  user["email"] = row["email"].isNull() ? "" : row["email"].as<std::string>();
                  user["email_verified"] =
                    row["email_verified"].isNull() ? false : row["email_verified"].as<bool>();
                  user["mfa_enabled"] =
                    row["mfa_enabled"].isNull() ? false : row["mfa_enabled"].as<bool>();
                  users.append(user);
              }

              json["users"] = users;
              json["total"] = static_cast<int>(result.size());
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch users: ") + e.base().what()
              );
          }
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void UserAdminController::disableUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "userId is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "UPDATE users SET locked_until = 9999999999 WHERE id = $1",
          [sharedCb, req, userId](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found");
                  return;
              }

              Json::Value json;
              json["status"] = "success";
              json["message"] = "User disabled successfully";
              json["user_id"] = userId;
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              (*sharedCb)(resp);
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to disable user: ") + e.base().what()
              );
          },
          userId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void UserAdminController::assignUserRoles(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "userId is required");
        return;
    }

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("roles") || !(*jsonBody)["roles"].isArray())
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "Request body must contain a 'roles' array"
        );
        return;
    }

    std::vector<std::string> roles;
    for (const auto &role : (*jsonBody)["roles"])
    {
        if (role.isString())
        {
            roles.push_back(role.asString());
        }
    }

    try
    {
        auto db = ::drogon::app().getDbClient();

        // Step 1: Delete existing roles for this user
        db->execSqlAsync(
          "DELETE FROM user_roles WHERE user_id = $1",
          [sharedCb, req, userId, roles, db](const ::drogon::orm::Result &) {
              if (roles.empty())
              {
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "User roles updated successfully";
                  json["user_id"] = userId;
                  json["roles"] = Json::Value(Json::arrayValue);
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                  (*sharedCb)(resp);
                  return;
              }

              // Step 2: Insert new roles
              auto remaining = std::make_shared<std::atomic<int>>(static_cast<int>(roles.size()));
              auto assignedRoles = std::make_shared<std::vector<std::string>>();
              auto mu = std::make_shared<std::mutex>();

              for (const auto &roleName : roles)
              {
                  db->execSqlAsync(
                    "INSERT INTO user_roles (user_id, role_id) "
                    "SELECT $1, id FROM roles WHERE name = $2",
                    [sharedCb, req, userId, roleName, remaining, assignedRoles, mu](
                      const ::drogon::orm::Result &result
                    ) {
                        if (result.affectedRows() > 0)
                        {
                            std::lock_guard<std::mutex> lock(*mu);
                            assignedRoles->push_back(roleName);
                        }

                        if (remaining->fetch_sub(1) == 1)
                        {
                            // All inserts completed
                            Json::Value json;
                            json["status"] = "success";
                            json["message"] = "User roles updated successfully";
                            json["user_id"] = userId;
                            Json::Value rolesJson(Json::arrayValue);
                            {
                                std::lock_guard<std::mutex> lock(*mu);
                                for (const auto &r : *assignedRoles)
                                    rolesJson.append(r);
                            }
                            json["roles"] = rolesJson;
                            auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                            (*sharedCb)(resp);
                        }
                    },
                    [sharedCb, req, remaining](const ::drogon::orm::DrogonDbException &e) {
                        if (remaining->fetch_sub(1) == 1)
                        {
                            respondError(
                              req,
                              sharedCb,
                              "DB_QUERY_ERROR",
                              std::string("Failed to assign some roles: ") + e.base().what()
                            );
                        }
                    },
                    userId,
                    roleName
                  );
              }
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to clear existing roles: ") + e.base().what()
              );
          },
          userId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void UserAdminController::getUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "userId is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT u.id, u.username, u.email, u.email_verified, u.mfa_enabled, "
          "u.failed_login_count, u.locked_until, u.created_at, "
          "COALESCE(json_agg(r.name) FILTER (WHERE r.name IS NOT NULL), '[]') AS roles "
          "FROM users u "
          "LEFT JOIN user_roles ur ON u.id = ur.user_id "
          "LEFT JOIN roles r ON ur.role_id = r.id "
          "WHERE u.id = $1 "
          "GROUP BY u.id",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              if (result.empty())
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found");
                  return;
              }
              const auto &row = result[0];
              Json::Value json;
              json["status"] = "success";
              json["id"] = row["id"].as<int>();
              json["username"] = row["username"].as<std::string>();
              json["email"] = row["email"].isNull() ? "" : row["email"].as<std::string>();
              json["email_verified"] =
                row["email_verified"].isNull() ? false : row["email_verified"].as<bool>();
              json["mfa_enabled"] =
                row["mfa_enabled"].isNull() ? false : row["mfa_enabled"].as<bool>();
              json["failed_login_count"] =
                row["failed_login_count"].isNull() ? 0 : row["failed_login_count"].as<int>();
              int64_t lockedUntil =
                row["locked_until"].isNull() ? 0 : row["locked_until"].as<int64_t>();
              auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch()
              )
                           .count();
              json["locked"] = (lockedUntil > now);
              json["locked_until"] = lockedUntil;
              json["created_at"] =
                row["created_at"].isNull() ? "" : row["created_at"].as<std::string>();
              // Parse roles JSON array from aggregation
              std::string rolesStr = row["roles"].isNull() ? "[]" : row["roles"].as<std::string>();
              Json::Value rolesJson;
              Json::CharReaderBuilder builder;
              std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
              std::string parseErrors;
              if (
                reader->parse(
                  rolesStr.c_str(), rolesStr.c_str() + rolesStr.size(), &rolesJson, &parseErrors
                ) &&
                rolesJson.isArray()
              )
              {
                  json["roles"] = rolesJson;
              }
              else
              {
                  json["roles"] = Json::Value(Json::arrayValue);
              }
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch user: ") + e.base().what()
              );
          },
          userId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void UserAdminController::updateUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "userId is required");
        return;
    }

    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "Invalid JSON body");
        return;
    }

    std::vector<std::string> setClauses;
    std::vector<std::string> params;
    int paramIdx = 1;

    if (jsonBody->isMember("email"))
    {
        // Normalize on write so admin edits stay consistent with registration
        // (login + password reset look up the canonical form).
        setClauses.push_back("email = $" + std::to_string(paramIdx++));
        params.push_back(::oauth2::utils::normalizeEmail((*jsonBody)["email"].asString()));
    }
    if (jsonBody->isMember("email_verified"))
    {
        setClauses.push_back("email_verified = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["email_verified"].asBool() ? "true" : "false");
    }

    if (setClauses.empty())
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "No updatable fields provided");
        return;
    }

    std::string query = "UPDATE users SET ";
    for (size_t i = 0; i < setClauses.size(); ++i)
    {
        if (i > 0)
            query += ", ";
        query += setClauses[i];
    }
    query += " WHERE id = $" + std::to_string(paramIdx);
    params.push_back(userId);

    try
    {
        auto db = ::drogon::app().getDbClient();
        if (params.size() == 2)
        {
            db->execSqlAsync(
              query,
              [sharedCb, req, userId](const ::drogon::orm::Result &result) {
                  if (result.affectedRows() == 0)
                  {
                      respondError(
                        req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found"
                      );
                      return;
                  }
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "User updated successfully";
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to update user: ") + e.base().what()
                  );
              },
              params[0],
              params[1]
            );
        }
        else if (params.size() == 3)
        {
            db->execSqlAsync(
              query,
              [sharedCb, req](const ::drogon::orm::Result &result) {
                  if (result.affectedRows() == 0)
                  {
                      respondError(
                        req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found"
                      );
                      return;
                  }
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "User updated successfully";
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to update user: ") + e.base().what()
                  );
              },
              params[0],
              params[1],
              params[2]
            );
        }
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void UserAdminController::enableUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "userId is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "UPDATE users SET locked_until = 0, failed_login_count = 0 WHERE id = $1",
          [sharedCb, req, userId](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "User not found");
                  return;
              }
              Json::Value json;
              json["status"] = "success";
              json["message"] = "User enabled successfully";
              json["user_id"] = userId;
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to enable user: ") + e.base().what()
              );
          },
          userId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void UserAdminController::getUserRoles(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &userId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "userId is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT r.id, r.name, r.description FROM roles r "
          "JOIN user_roles ur ON r.id = ur.role_id "
          "WHERE ur.user_id = $1 ORDER BY r.name",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              Json::Value json;
              json["status"] = "success";
              Json::Value roles(Json::arrayValue);
              for (const auto &row : result)
              {
                  Json::Value role;
                  role["id"] = row["id"].as<int>();
                  role["name"] = row["name"].as<std::string>();
                  role["description"] =
                    row["description"].isNull() ? "" : row["description"].as<std::string>();
                  roles.append(role);
              }
              json["roles"] = roles;
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch user roles: ") + e.base().what()
              );
          },
          userId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

}  // namespace authforge::drogon::controllers
