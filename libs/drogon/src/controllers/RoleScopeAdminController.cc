#include <authforge/drogon/controllers/RoleScopeAdminController.h>
#include <drogon/drogon.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <atomic>
#include <memory>
#include <mutex>

// M5 Task 29a (authforge-sdk-refactor): role + scope management routes moved
// verbatim from AdminController.cc. respondError helper, OpenAPI docs struct,
// and every handler body are byte-for-byte copies -- no behavior change (Admin
// API tests must stay green). The raw SQL and business logic remain inline
// pending Task 29b extraction.

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

void RoleScopeAdminController::listRoles(
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
          "SELECT r.id, r.name, r.description, r.created_at, "
          "COUNT(DISTINCT ur.user_id) AS user_count "
          "FROM roles r "
          "LEFT JOIN user_roles ur ON r.id = ur.role_id "
          "GROUP BY r.id ORDER BY r.name",
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
                  role["user_count"] = row["user_count"].as<int>();
                  role["created_at"] =
                    row["created_at"].isNull() ? "" : row["created_at"].as<std::string>();
                  roles.append(role);
              }
              json["roles"] = roles;
              json["total"] = static_cast<int>(result.size());
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch roles: ") + e.base().what()
              );
          }
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void RoleScopeAdminController::createRole(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("name"))
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "Request body must contain 'name'"
        );
        return;
    }

    std::string name = (*jsonBody)["name"].asString();
    std::string description = jsonBody->get("description", "").asString();

    if (name.empty())
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "Role name cannot be empty"
        );
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        // Check for existing role first to return proper 409
        db->execSqlAsync(
          "SELECT id FROM roles WHERE name = $1",
          [sharedCb, req, db, name, description](const ::drogon::orm::Result &checkResult) {
              if (!checkResult.empty())
              {
                  respondError(
                    req, sharedCb, "VALIDATION_RESOURCE_CONFLICT", "Role name already exists"
                  );
                  return;
              }
              db->execSqlAsync(
                "INSERT INTO roles (name, description) VALUES ($1, $2) "
                "RETURNING id, name, description",
                [sharedCb, req](const ::drogon::orm::Result &result) {
                    if (result.empty())
                    {
                        respondError(req, sharedCb, "INTERNAL_ERROR", "Failed to create role");
                        return;
                    }
                    const auto &row = result[0];
                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "Role created successfully";
                    json["id"] = row["id"].as<int>();
                    json["name"] = row["name"].as<std::string>();
                    json["description"] =
                      row["description"].isNull() ? "" : row["description"].as<std::string>();
                    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                    resp->setStatusCode(::drogon::k201Created);
                    (*sharedCb)(resp);
                },
                [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                    respondError(
                      req,
                      sharedCb,
                      "DB_QUERY_ERROR",
                      std::string("Failed to create role: ") + e.base().what()
                    );
                },
                name,
                description
              );
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Database error checking role name: ") + e.base().what()
              );
          },
          name
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void RoleScopeAdminController::updateRole(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &roleId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "Invalid JSON body");
        return;
    }

    std::vector<std::string> setClauses;
    std::vector<std::string> params;
    int paramIdx = 1;

    if (jsonBody->isMember("description"))
    {
        setClauses.push_back("description = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["description"].asString());
    }

    if (setClauses.empty())
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "No updatable fields provided");
        return;
    }

    std::string query = "UPDATE roles SET ";
    for (size_t i = 0; i < setClauses.size(); ++i)
    {
        if (i > 0)
            query += ", ";
        query += setClauses[i];
    }
    query += " WHERE id = $" + std::to_string(paramIdx);
    params.push_back(roleId);

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          query,
          [sharedCb, req](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "Role not found");
                  return;
              }
              Json::Value json;
              json["status"] = "success";
              json["message"] = "Role updated successfully";
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to update role: ") + e.base().what()
              );
          },
          params[0],
          params[1]
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void RoleScopeAdminController::deleteRole(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &roleId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "DELETE FROM roles WHERE id = $1 AND name NOT IN ('admin', 'user')",
          [sharedCb, req, roleId](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(
                    req,
                    sharedCb,
                    "VALIDATION_RESOURCE_NOT_FOUND",
                    "Role not found or cannot delete built-in roles"
                  );
                  return;
              }
              Json::Value json;
              json["status"] = "success";
              json["message"] = "Role deleted successfully";
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to delete role: ") + e.base().what()
              );
          },
          roleId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void RoleScopeAdminController::listScopes(
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
          "SELECT id, name, description, mapped_role, is_default, requires_admin_role "
          "FROM oauth2_scopes ORDER BY id",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              Json::Value json;
              json["status"] = "success";
              Json::Value scopes(Json::arrayValue);

              for (const auto &row : result)
              {
                  Json::Value scope;
                  scope["id"] = row["id"].as<int>();
                  scope["name"] = row["name"].as<std::string>();
                  scope["description"] =
                    row["description"].isNull() ? "" : row["description"].as<std::string>();
                  scope["mapped_role"] =
                    row["mapped_role"].isNull() ? "" : row["mapped_role"].as<std::string>();
                  scope["is_default"] =
                    row["is_default"].isNull() ? false : row["is_default"].as<bool>();
                  scope["requires_admin_role"] = row["requires_admin_role"].isNull()
                                                   ? false
                                                   : row["requires_admin_role"].as<bool>();
                  scopes.append(scope);
              }

              json["scopes"] = scopes;
              json["total"] = static_cast<int>(result.size());
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to fetch scopes: ") + e.base().what()
              );
          }
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void RoleScopeAdminController::createScope(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("name"))
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "Request body must contain 'name'"
        );
        return;
    }

    std::string name = (*jsonBody)["name"].asString();
    std::string description = jsonBody->get("description", "").asString();
    std::string mappedRole = jsonBody->get("mapped_role", "").asString();
    bool isDefault = jsonBody->get("is_default", false).asBool();
    bool requiresAdminRole = jsonBody->get("requires_admin_role", false).asBool();

    if (name.empty())
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "Scope name cannot be empty"
        );
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "SELECT id FROM oauth2_scopes WHERE name = $1",
          [sharedCb, req, db, name, description, mappedRole, isDefault, requiresAdminRole](
            const ::drogon::orm::Result &checkResult
          ) {
              if (!checkResult.empty())
              {
                  respondError(
                    req, sharedCb, "VALIDATION_RESOURCE_CONFLICT", "Scope name already exists"
                  );
                  return;
              }
              db->execSqlAsync(
                "INSERT INTO oauth2_scopes (name, description, mapped_role, is_default, "
                "requires_admin_role) VALUES ($1, $2, $3, $4, $5) "
                "RETURNING id, name, description, mapped_role, is_default, requires_admin_role",
                [sharedCb, req](const ::drogon::orm::Result &result) {
                    if (result.empty())
                    {
                        respondError(req, sharedCb, "INTERNAL_ERROR", "Failed to create scope");
                        return;
                    }
                    const auto &row = result[0];
                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "Scope created successfully";
                    json["id"] = row["id"].as<int>();
                    json["name"] = row["name"].as<std::string>();
                    json["description"] =
                      row["description"].isNull() ? "" : row["description"].as<std::string>();
                    json["mapped_role"] =
                      row["mapped_role"].isNull() ? "" : row["mapped_role"].as<std::string>();
                    json["is_default"] =
                      row["is_default"].isNull() ? false : row["is_default"].as<bool>();
                    json["requires_admin_role"] = row["requires_admin_role"].isNull()
                                                    ? false
                                                    : row["requires_admin_role"].as<bool>();
                    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                    resp->setStatusCode(::drogon::k201Created);
                    (*sharedCb)(resp);
                },
                [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                    respondError(
                      req,
                      sharedCb,
                      "DB_QUERY_ERROR",
                      std::string("Failed to create scope: ") + e.base().what()
                    );
                },
                name,
                description,
                mappedRole.empty() ? nullptr : mappedRole.c_str(),
                isDefault,
                requiresAdminRole
              );
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Database error checking scope name: ") + e.base().what()
              );
          },
          name
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void RoleScopeAdminController::updateScope(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &scopeId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "Invalid JSON body");
        return;
    }

    std::vector<std::string> setClauses;
    std::vector<std::string> params;
    int paramIdx = 1;

    if (jsonBody->isMember("description"))
    {
        setClauses.push_back("description = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["description"].asString());
    }
    if (jsonBody->isMember("mapped_role"))
    {
        setClauses.push_back("mapped_role = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["mapped_role"].asString());
    }
    if (jsonBody->isMember("is_default"))
    {
        setClauses.push_back("is_default = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["is_default"].asBool() ? "true" : "false");
    }
    if (jsonBody->isMember("requires_admin_role"))
    {
        setClauses.push_back("requires_admin_role = $" + std::to_string(paramIdx++));
        params.push_back((*jsonBody)["requires_admin_role"].asBool() ? "true" : "false");
    }

    if (setClauses.empty())
    {
        respondError(req, sharedCb, "VALIDATION_INVALID_INPUT", "No updatable fields provided");
        return;
    }

    std::string query = "UPDATE oauth2_scopes SET ";
    for (size_t i = 0; i < setClauses.size(); ++i)
    {
        if (i > 0)
            query += ", ";
        query += setClauses[i];
    }
    query += " WHERE id = $" + std::to_string(paramIdx);
    params.push_back(scopeId);

    try
    {
        auto db = ::drogon::app().getDbClient();
        // Use a lambda that captures params by value and dispatches based on count
        auto execUpdate = [&](auto &&...args) {
            db->execSqlAsync(
              query,
              [sharedCb, req](const ::drogon::orm::Result &result) {
                  if (result.affectedRows() == 0)
                  {
                      respondError(
                        req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "Scope not found"
                      );
                      return;
                  }
                  Json::Value json;
                  json["status"] = "success";
                  json["message"] = "Scope updated successfully";
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to update scope: ") + e.base().what()
                  );
              },
              std::forward<decltype(args)>(args)...
            );
        };

        if (params.size() == 2)
            execUpdate(params[0], params[1]);
        else if (params.size() == 3)
            execUpdate(params[0], params[1], params[2]);
        else if (params.size() == 4)
            execUpdate(params[0], params[1], params[2], params[3]);
        else if (params.size() == 5)
            execUpdate(params[0], params[1], params[2], params[3], params[4]);
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void RoleScopeAdminController::deleteScope(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &scopeId
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();
        db->execSqlAsync(
          "DELETE FROM oauth2_scopes WHERE id = $1 "
          "AND name NOT IN ('openid', 'profile', 'email', 'admin')",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(
                    req,
                    sharedCb,
                    "VALIDATION_RESOURCE_NOT_FOUND",
                    "Scope not found or cannot delete built-in scopes"
                  );
                  return;
              }
              Json::Value json;
              json["status"] = "success";
              json["message"] = "Scope deleted successfully";
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to delete scope: ") + e.base().what()
              );
          },
          scopeId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

}  // namespace authforge::drogon::controllers
