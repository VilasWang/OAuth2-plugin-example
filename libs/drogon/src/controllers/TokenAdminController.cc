#include <authforge/drogon/controllers/TokenAdminController.h>
#include <drogon/drogon.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <memory>

// M5 Task 29a (authforge-sdk-refactor): token-management routes moved verbatim
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

struct TokenAdminControllerDocs
{
    TokenAdminControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo listTokens;
        listTokens.path = "/api/admin/tokens";
        listTokens.method = "GET";
        listTokens.summary = "List Tokens";
        listTokens.description = "Get a list of active OAuth2 tokens.";
        listTokens.tags = {"Admin", "Tokens"};
        listTokens.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(listTokens);

        ::authforge::drogon::observability::openapi::EndpointInfo revokeTokensByClient;
        revokeTokensByClient.path = "/api/admin/tokens/revoke-by-client";
        revokeTokensByClient.method = "POST";
        revokeTokensByClient.summary = "Revoke Tokens By Client";
        revokeTokensByClient.description = "Revoke all tokens issued to a specific client.";
        revokeTokensByClient.tags = {"Admin", "Tokens"};
        revokeTokensByClient.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(
          revokeTokensByClient
        );

        ::authforge::drogon::observability::openapi::EndpointInfo revokeTokensByUser;
        revokeTokensByUser.path = "/api/admin/tokens/revoke-by-user";
        revokeTokensByUser.method = "POST";
        revokeTokensByUser.summary = "Revoke Tokens By User";
        revokeTokensByUser.description = "Revoke all tokens issued for a specific user.";
        revokeTokensByUser.tags = {"Admin", "Tokens"};
        revokeTokensByUser.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(
          revokeTokensByUser
        );

        ::authforge::drogon::observability::openapi::EndpointInfo revokeToken;
        revokeToken.path = "/api/admin/tokens/{tokenPrefix}";
        revokeToken.method = "DELETE";
        revokeToken.summary = "Revoke Token";
        revokeToken.description = "Revoke a specific token by its prefix.";
        revokeToken.tags = {"Admin", "Tokens"};
        revokeToken.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(revokeToken);

        ::authforge::drogon::observability::openapi::EndpointInfo getOidcKeys;
        getOidcKeys.path = "/api/admin/oidc/keys";
        getOidcKeys.method = "GET";
        getOidcKeys.summary = "Get OIDC Keys Info";
        getOidcKeys.description = "Get information about OIDC signing keys.";
        getOidcKeys.tags = {"Admin", "OIDC"};
        getOidcKeys.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(getOidcKeys);
    }
} g_tokenAdminControllerDocs;
}  // namespace

void TokenAdminController::listTokens(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    int page = 1;
    int perPage = 50;
    std::string clientIdFilter = req->getParameter("client_id");
    std::string userIdFilter = req->getParameter("user_id");

    try
    {
        page = std::stoi(req->getParameter("page"));
    }
    catch (...)
    {
    }
    try
    {
        perPage = std::stoi(req->getParameter("per_page"));
    }
    catch (...)
    {
    }
    if (perPage > 100)
        perPage = 100;
    if (perPage < 1)
        perPage = 50;
    if (page < 1)
        page = 1;
    int offset = (page - 1) * perPage;

    try
    {
        auto db = ::drogon::app().getDbClient();

        // Build count query and data query with filters
        // expires_at is stored as BIGINT (Unix epoch seconds)
        std::string whereClause =
          " WHERE expires_at > EXTRACT(EPOCH FROM NOW())::BIGINT AND (revoked IS NULL OR revoked = "
          "FALSE)";
        std::string filterParams;
        int paramIdx = 1;

        if (!clientIdFilter.empty())
        {
            whereClause += " AND client_id = $" + std::to_string(paramIdx++);
        }
        if (!userIdFilter.empty())
        {
            whereClause += " AND user_id = $" + std::to_string(paramIdx++);
        }

        std::string countQuery = "SELECT COUNT(*) as total FROM oauth2_access_tokens" + whereClause;
        std::string dataQuery =
          "SELECT token, client_id, user_id, scope, issued_at, expires_at "
          "FROM oauth2_access_tokens" +
          whereClause + " ORDER BY issued_at DESC LIMIT " + std::to_string(perPage) + " OFFSET " +
          std::to_string(offset);

        // Execute based on filter combination
        if (!clientIdFilter.empty() && !userIdFilter.empty())
        {
            // Both filters
            db->execSqlAsync(
              countQuery,
              [sharedCb, req, dataQuery, page, perPage, clientIdFilter, userIdFilter, db](
                const ::drogon::orm::Result &countResult
              ) {
                  int total = 0;
                  if (!countResult.empty())
                  {
                      total = countResult[0]["total"].as<int>();
                  }

                  db->execSqlAsync(
                    dataQuery,
                    [sharedCb, req, page, perPage, total](const ::drogon::orm::Result &result) {
                        Json::Value json;
                        Json::Value tokens(Json::arrayValue);

                        for (const auto &row : result)
                        {
                            Json::Value token;
                            std::string fullToken = row["token"].as<std::string>();
                            token["token_prefix"] = fullToken.substr(0, 8);
                            token["client_id"] =
                              row["client_id"].isNull() ? "" : row["client_id"].as<std::string>();
                            token["user_id"] =
                              row["user_id"].isNull() ? "" : row["user_id"].as<std::string>();
                            token["scope"] =
                              row["scope"].isNull() ? "" : row["scope"].as<std::string>();
                            token["created_at"] =
                              row["issued_at"].isNull()
                                ? ""
                                : std::to_string(row["issued_at"].as<int64_t>());
                            token["expires_at"] =
                              row["expires_at"].isNull()
                                ? ""
                                : std::to_string(row["expires_at"].as<int64_t>());
                            tokens.append(token);
                        }

                        json["tokens"] = tokens;
                        json["total"] = total;
                        json["page"] = page;
                        json["per_page"] = perPage;
                        (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                    },
                    [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                        respondError(
                          req,
                          sharedCb,
                          "DB_QUERY_ERROR",
                          std::string("Failed to fetch tokens: ") + e.base().what()
                        );
                    },
                    clientIdFilter,
                    userIdFilter
                  );
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to count tokens: ") + e.base().what()
                  );
              },
              clientIdFilter,
              userIdFilter
            );
        }
        else if (!clientIdFilter.empty())
        {
            // Only client_id filter
            db->execSqlAsync(
              countQuery,
              [sharedCb, req, dataQuery, page, perPage, clientIdFilter, db](
                const ::drogon::orm::Result &countResult
              ) {
                  int total = 0;
                  if (!countResult.empty())
                  {
                      total = countResult[0]["total"].as<int>();
                  }

                  db->execSqlAsync(
                    dataQuery,
                    [sharedCb, req, page, perPage, total](const ::drogon::orm::Result &result) {
                        Json::Value json;
                        Json::Value tokens(Json::arrayValue);

                        for (const auto &row : result)
                        {
                            Json::Value token;
                            std::string fullToken = row["token"].as<std::string>();
                            token["token_prefix"] = fullToken.substr(0, 8);
                            token["client_id"] =
                              row["client_id"].isNull() ? "" : row["client_id"].as<std::string>();
                            token["user_id"] =
                              row["user_id"].isNull() ? "" : row["user_id"].as<std::string>();
                            token["scope"] =
                              row["scope"].isNull() ? "" : row["scope"].as<std::string>();
                            token["created_at"] =
                              row["issued_at"].isNull()
                                ? ""
                                : std::to_string(row["issued_at"].as<int64_t>());
                            token["expires_at"] =
                              row["expires_at"].isNull()
                                ? ""
                                : std::to_string(row["expires_at"].as<int64_t>());
                            tokens.append(token);
                        }

                        json["tokens"] = tokens;
                        json["total"] = total;
                        json["page"] = page;
                        json["per_page"] = perPage;
                        (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                    },
                    [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                        respondError(
                          req,
                          sharedCb,
                          "DB_QUERY_ERROR",
                          std::string("Failed to fetch tokens: ") + e.base().what()
                        );
                    },
                    clientIdFilter
                  );
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to count tokens: ") + e.base().what()
                  );
              },
              clientIdFilter
            );
        }
        else if (!userIdFilter.empty())
        {
            // Only user_id filter
            db->execSqlAsync(
              countQuery,
              [sharedCb, req, dataQuery, page, perPage, userIdFilter, db](
                const ::drogon::orm::Result &countResult
              ) {
                  int total = 0;
                  if (!countResult.empty())
                  {
                      total = countResult[0]["total"].as<int>();
                  }

                  db->execSqlAsync(
                    dataQuery,
                    [sharedCb, req, page, perPage, total](const ::drogon::orm::Result &result) {
                        Json::Value json;
                        Json::Value tokens(Json::arrayValue);

                        for (const auto &row : result)
                        {
                            Json::Value token;
                            std::string fullToken = row["token"].as<std::string>();
                            token["token_prefix"] = fullToken.substr(0, 8);
                            token["client_id"] =
                              row["client_id"].isNull() ? "" : row["client_id"].as<std::string>();
                            token["user_id"] =
                              row["user_id"].isNull() ? "" : row["user_id"].as<std::string>();
                            token["scope"] =
                              row["scope"].isNull() ? "" : row["scope"].as<std::string>();
                            token["created_at"] =
                              row["issued_at"].isNull()
                                ? ""
                                : std::to_string(row["issued_at"].as<int64_t>());
                            token["expires_at"] =
                              row["expires_at"].isNull()
                                ? ""
                                : std::to_string(row["expires_at"].as<int64_t>());
                            tokens.append(token);
                        }

                        json["tokens"] = tokens;
                        json["total"] = total;
                        json["page"] = page;
                        json["per_page"] = perPage;
                        (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                    },
                    [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                        respondError(
                          req,
                          sharedCb,
                          "DB_QUERY_ERROR",
                          std::string("Failed to fetch tokens: ") + e.base().what()
                        );
                    },
                    userIdFilter
                  );
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to count tokens: ") + e.base().what()
                  );
              },
              userIdFilter
            );
        }
        else
        {
            // No filters
            db->execSqlAsync(
              countQuery,
              [sharedCb, req, dataQuery, page, perPage, db](
                const ::drogon::orm::Result &countResult
              ) {
                  int total = 0;
                  if (!countResult.empty())
                  {
                      total = countResult[0]["total"].as<int>();
                  }

                  db->execSqlAsync(
                    dataQuery,
                    [sharedCb, req, page, perPage, total](const ::drogon::orm::Result &result) {
                        Json::Value json;
                        Json::Value tokens(Json::arrayValue);

                        for (const auto &row : result)
                        {
                            Json::Value token;
                            std::string fullToken = row["token"].as<std::string>();
                            token["token_prefix"] = fullToken.substr(0, 8);
                            token["client_id"] =
                              row["client_id"].isNull() ? "" : row["client_id"].as<std::string>();
                            token["user_id"] =
                              row["user_id"].isNull() ? "" : row["user_id"].as<std::string>();
                            token["scope"] =
                              row["scope"].isNull() ? "" : row["scope"].as<std::string>();
                            token["created_at"] =
                              row["issued_at"].isNull()
                                ? ""
                                : std::to_string(row["issued_at"].as<int64_t>());
                            token["expires_at"] =
                              row["expires_at"].isNull()
                                ? ""
                                : std::to_string(row["expires_at"].as<int64_t>());
                            tokens.append(token);
                        }

                        json["tokens"] = tokens;
                        json["total"] = total;
                        json["page"] = page;
                        json["per_page"] = perPage;
                        (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                    },
                    [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                        respondError(
                          req,
                          sharedCb,
                          "DB_QUERY_ERROR",
                          std::string("Failed to fetch tokens: ") + e.base().what()
                        );
                    }
                  );
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to count tokens: ") + e.base().what()
                  );
              }
            );
        }
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void TokenAdminController::revokeToken(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &tokenPrefix
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (tokenPrefix.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "tokenPrefix is required");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        std::string likePattern = tokenPrefix + "%";

        db->execSqlAsync(
          "DELETE FROM oauth2_access_tokens WHERE token LIKE $1",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              if (result.affectedRows() == 0)
              {
                  respondError(req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "Token not found");
                  return;
              }

              Json::Value json;
              json["status"] = "success";
              json["message"] = "Token revoked";
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to revoke token: ") + e.base().what()
              );
          },
          likePattern
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void TokenAdminController::revokeTokensByClient(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("client_id"))
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "Request body must contain 'client_id'"
        );
        return;
    }

    std::string clientId = (*jsonBody)["client_id"].asString();
    if (clientId.empty())
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "client_id cannot be empty"
        );
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();

        // Delete access tokens for this client
        db->execSqlAsync(
          "DELETE FROM oauth2_access_tokens WHERE client_id = $1",
          [sharedCb, req, clientId, db](const ::drogon::orm::Result &accessResult) {
              int accessCount = static_cast<int>(accessResult.affectedRows());

              // Also delete refresh tokens for this client
              db->execSqlAsync(
                "DELETE FROM oauth2_refresh_tokens WHERE client_id = $1",
                [sharedCb, req, clientId, accessCount](const ::drogon::orm::Result &refreshResult) {
                    int totalCount = accessCount + static_cast<int>(refreshResult.affectedRows());

                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "All tokens for client revoked";
                    json["count"] = totalCount;
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                },
                [sharedCb, req, accessCount](const ::drogon::orm::DrogonDbException &) {
                    // Refresh token deletion failed but access tokens were deleted
                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "Access tokens revoked (refresh token cleanup failed)";
                    json["count"] = accessCount;
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                },
                clientId
              );
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to revoke tokens: ") + e.base().what()
              );
          },
          clientId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "Database unavailable");
    }
}

void TokenAdminController::revokeTokensByUser(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("user_id"))
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "Request body must contain 'user_id'"
        );
        return;
    }

    std::string userId = (*jsonBody)["user_id"].asString();
    if (userId.empty())
    {
        respondError(req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "user_id cannot be empty");
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();

        // Delete access tokens for this user
        db->execSqlAsync(
          "DELETE FROM oauth2_access_tokens WHERE user_id = $1",
          [sharedCb, req, userId, db](const ::drogon::orm::Result &accessResult) {
              int accessCount = static_cast<int>(accessResult.affectedRows());

              // Also delete refresh tokens for this user
              db->execSqlAsync(
                "DELETE FROM oauth2_refresh_tokens WHERE user_id = $1",
                [sharedCb, req, userId, accessCount](const ::drogon::orm::Result &refreshResult) {
                    int totalCount = accessCount + static_cast<int>(refreshResult.affectedRows());

                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "All tokens for user revoked";
                    json["count"] = totalCount;
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                },
                [sharedCb, req, accessCount](const ::drogon::orm::DrogonDbException &) {
                    // Refresh token deletion failed but access tokens were deleted
                    Json::Value json;
                    json["status"] = "success";
                    json["message"] = "Access tokens revoked (refresh token cleanup failed)";
                    json["count"] = accessCount;
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                },
                userId
              );
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("Failed to revoke tokens: ") + e.base().what()
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

void TokenAdminController::getOidcKeys(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    Json::Value json;
    json["status"] = "success";
    json["kid"] = "default-key-1";
    json["kty"] = "RSA";
    json["alg"] = "RS256";
    json["use"] = "sig";
    json["jwks_uri"] = "/.well-known/jwks.json";
    json["discovery_uri"] = "/.well-known/openid-configuration";
    json["key_status"] = "active";
    json["note"] = "Key rotation is not yet implemented. Single signing key in use.";

    callback(::drogon::HttpResponse::newHttpJsonResponse(json));
}

}  // namespace authforge::drogon::controllers
