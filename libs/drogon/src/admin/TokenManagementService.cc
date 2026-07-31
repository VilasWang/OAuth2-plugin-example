#include <authforge/drogon/admin/TokenManagementService.h>

#include <authforge/storage/postgres/models/Oauth2AccessTokens.h>
#include <authforge/storage/postgres/models/Oauth2RefreshTokens.h>
#include <authforge/drogon/error/ErrorResponder.h>

#include <drogon/drogon.h>

#include <ctime>

namespace authforge::drogon::admin
{

namespace
{
void respondError(
  const ::drogon::HttpRequestPtr &req,
  const TokenManagementService::ResponseCallback &cb,
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

::drogon::orm::DbClientPtr getDbOrRespond(
  const ::drogon::HttpRequestPtr &req,
  const TokenManagementService::ResponseCallback &cb
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

// Build the JSON token object for one access-token row. Mirrors the original
// handler's per-row mapping (token_prefix = first 8 chars, created_at/expires_at
// as stringified int64). Factored out since listTokens used to inline this 4x.
Json::Value tokenRowToJson(const Oauth2AccessTokens &row)
{
    Json::Value token;
    std::string fullToken = row.getValueOfToken();
    token["token_prefix"] = fullToken.substr(0, 8);
    token["client_id"] = row.getValueOfClientId();
    token["user_id"] = row.getValueOfUserId();
    token["scope"] = row.getValueOfScope();
    token["created_at"] = std::to_string(row.getValueOfIssuedAt());
    token["expires_at"] = std::to_string(row.getValueOfExpiresAt());
    return token;
}
}  // namespace

void TokenManagementService::listTokens(const ::drogon::HttpRequestPtr &req, ResponseCallback cb)
{
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

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    // Active = not expired AND not revoked. The original SQL used
    // `expires_at > EXTRACT(EPOCH FROM NOW())::BIGINT` (DB-server clock) and
    // `(revoked IS NULL OR revoked = FALSE)`. Here we evaluate now() in C++
    // (app clock) -- equivalent in practice (servers are NTP-synced) and keeps
    // the query pure-Criteria so it goes through Mapper (no raw SQL).
    int64_t now = static_cast<int64_t>(std::time(nullptr));

    Criteria active = Criteria(Oauth2AccessTokens::Cols::_expires_at, CompareOperator::GT, now) &&
                      (Criteria(Oauth2AccessTokens::Cols::_revoked, CompareOperator::EQ, false) ||
                       Criteria(Oauth2AccessTokens::Cols::_revoked, CompareOperator::IsNull));
    if (!clientIdFilter.empty())
    {
        active =
          active &&
          Criteria(Oauth2AccessTokens::Cols::_client_id, CompareOperator::EQ, clientIdFilter);
    }
    if (!userIdFilter.empty())
    {
        active =
          active && Criteria(Oauth2AccessTokens::Cols::_user_id, CompareOperator::EQ, userIdFilter);
    }

    Mapper<Oauth2AccessTokens> mapper(db);
    // Count first (matches original count-then-data two-step flow).
    mapper.count(
      active,
      [cb, req, active, page, perPage, db](const size_t total) {
          Mapper<Oauth2AccessTokens> dataMapper(db);
          dataMapper.paginate(page, perPage)
            .orderBy(Oauth2AccessTokens::Cols::_issued_at, SortOrder::DESC)
            .findBy(
              active,
              [cb, page, perPage, total](const std::vector<Oauth2AccessTokens> &rows) {
                  Json::Value json;
                  Json::Value tokens(Json::arrayValue);
                  for (const auto &row : rows)
                  {
                      tokens.append(tokenRowToJson(row));
                  }
                  json["tokens"] = tokens;
                  json["total"] = static_cast<Json::UInt64>(total);
                  json["page"] = page;
                  json["per_page"] = perPage;
                  (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
              },
              [req, cb](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    cb,
                    "DB_QUERY_ERROR",
                    std::string("Failed to fetch tokens: ") + e.base().what()
                  );
              }
            );
      },
      [req, cb](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, cb, "DB_QUERY_ERROR", std::string("Failed to count tokens: ") + e.base().what()
          );
      }
    );
}

void TokenManagementService::revokeToken(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback cb,
  const std::string &tokenPrefix
)
{
    if (tokenPrefix.empty())
    {
        respondError(req, cb, "VALIDATION_MISSING_REQUIRED_FIELD", "tokenPrefix is required");
        return;
    }

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    Mapper<Oauth2AccessTokens> mapper(db);
    mapper.deleteBy(
      Criteria(Oauth2AccessTokens::Cols::_token, CompareOperator::Like, tokenPrefix + "%"),
      [cb, req](const size_t affected) {
          if (affected == 0)
          {
              respondError(req, cb, "VALIDATION_RESOURCE_NOT_FOUND", "Token not found");
              return;
          }
          Json::Value json;
          json["status"] = "success";
          json["message"] = "Token revoked";
          (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
      },
      [req, cb](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, cb, "DB_QUERY_ERROR", std::string("Failed to revoke token: ") + e.base().what()
          );
      }
    );
}

void TokenManagementService::revokeTokensByClient(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback cb
)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("client_id"))
    {
        respondError(
          req, cb, "VALIDATION_MISSING_REQUIRED_FIELD", "Request body must contain 'client_id'"
        );
        return;
    }

    std::string clientId = (*jsonBody)["client_id"].asString();
    if (clientId.empty())
    {
        respondError(req, cb, "VALIDATION_MISSING_REQUIRED_FIELD", "client_id cannot be empty");
        return;
    }

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    // Delete access tokens for this client first.
    Mapper<Oauth2AccessTokens> accessMapper(db);
    accessMapper.deleteBy(
      Criteria(Oauth2AccessTokens::Cols::_client_id, CompareOperator::EQ, clientId),
      [cb, req, clientId, db](const size_t accessCount) {
          // Then delete refresh tokens for this client (best-effort, same as the
          // original: refresh-cleanup failure still reports access success).
          Mapper<Oauth2RefreshTokens> refreshMapper(db);
          refreshMapper.deleteBy(
            Criteria(Oauth2RefreshTokens::Cols::_client_id, CompareOperator::EQ, clientId),
            [cb, accessCount](const size_t refreshCount) {
                Json::Value json;
                json["status"] = "success";
                json["message"] = "All tokens for client revoked";
                json["count"] = static_cast<Json::UInt64>(accessCount + refreshCount);
                (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
            },
            [cb, accessCount](const ::drogon::orm::DrogonDbException &) {
                Json::Value json;
                json["status"] = "success";
                json["message"] = "Access tokens revoked (refresh token cleanup failed)";
                json["count"] = static_cast<Json::UInt64>(accessCount);
                (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
            }
          );
      },
      [req, cb](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, cb, "DB_QUERY_ERROR", std::string("Failed to revoke tokens: ") + e.base().what()
          );
      }
    );
}

void TokenManagementService::revokeTokensByUser(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback cb
)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("user_id"))
    {
        respondError(
          req, cb, "VALIDATION_MISSING_REQUIRED_FIELD", "Request body must contain 'user_id'"
        );
        return;
    }

    std::string userId = (*jsonBody)["user_id"].asString();
    if (userId.empty())
    {
        respondError(req, cb, "VALIDATION_MISSING_REQUIRED_FIELD", "user_id cannot be empty");
        return;
    }

    auto db = getDbOrRespond(req, cb);
    if (!db)
    {
        return;
    }

    Mapper<Oauth2AccessTokens> accessMapper(db);
    accessMapper.deleteBy(
      Criteria(Oauth2AccessTokens::Cols::_user_id, CompareOperator::EQ, userId),
      [cb, req, userId, db](const size_t accessCount) {
          Mapper<Oauth2RefreshTokens> refreshMapper(db);
          refreshMapper.deleteBy(
            Criteria(Oauth2RefreshTokens::Cols::_user_id, CompareOperator::EQ, userId),
            [cb, accessCount](const size_t refreshCount) {
                Json::Value json;
                json["status"] = "success";
                json["message"] = "All tokens for user revoked";
                json["count"] = static_cast<Json::UInt64>(accessCount + refreshCount);
                (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
            },
            [cb, accessCount](const ::drogon::orm::DrogonDbException &) {
                Json::Value json;
                json["status"] = "success";
                json["message"] = "Access tokens revoked (refresh token cleanup failed)";
                json["count"] = static_cast<Json::UInt64>(accessCount);
                (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
            }
          );
      },
      [req, cb](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req, cb, "DB_QUERY_ERROR", std::string("Failed to revoke tokens: ") + e.base().what()
          );
      }
    );
}

void TokenManagementService::getOidcKeys(ResponseCallback cb)
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

    (*cb)(::drogon::HttpResponse::newHttpJsonResponse(json));
}

}  // namespace authforge::drogon::admin
