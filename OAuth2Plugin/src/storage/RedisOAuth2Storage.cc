#include <oauth2/storage/RedisOAuth2Storage.h>
#include <drogon/utils/Utilities.h>
#include <json/json.h>
#include <sstream>
#include <algorithm>
#include <oauth2/observability/OAuth2Metrics.h>

namespace oauth2
{

using namespace drogon;
using namespace drogon::nosql;

// Helper to safely parse JSON from Redis
static Json::Value parseJson(const std::string &jsonStr)
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream s(jsonStr);
    if (!Json::parseFromStream(builder, s, &root, &errs))
    {
        LOG_ERROR << "Redis JSON parse error: " << errs;
        return Json::nullValue;
    }
    return root;
}

// Helper to serialize JSON to string (replaces deprecated FastWriter)
static std::string jsonToString(const Json::Value &json)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";  // Compact output
    return Json::writeString(builder, json);
}

std::unique_ptr<IOAuth2Storage> createRedisStorage(const Json::Value &config)
{
    std::string clientName = config.get("client_name", "default").asString();
    return std::make_unique<RedisOAuth2Storage>(clientName);
}

void RedisOAuth2Storage::getClient(const std::string &clientId, ClientCallback &&cb)
{
    if (!redisClient_)
    {
        LOG_ERROR << "Redis client is not initialized!";
        cb(std::nullopt);
        return;
    }
    std::string cmd = "HGETALL oauth2:client:" + clientId;
    auto timer = std::make_shared<observability::OperationTimer>("getClient", "redis");
    redisClient_->execCommandAsync(
      [cb, clientId, timer](const RedisResult &result) {
          if (result.type() == RedisResultType::kNil || result.type() != RedisResultType::kArray)
          {
              cb(std::nullopt);
              return;
          }
          auto arr = result.asArray();
          if (arr.empty())
          {
              cb(std::nullopt);
              return;
          }

          OAuth2Client client;
          client.clientId = clientId;
          for (size_t i = 0; i < arr.size(); i += 2)
          {
              if (i + 1 >= arr.size())
                  break;
              std::string key = arr[i].asString();
              std::string val = arr[i + 1].asString();
              if (key == "secret")
                  client.clientSecretHash = val;
              else if (key == "salt")
                  client.salt = val;
              else if (key == "redirect_uris")
              {
                  auto json = parseJson(val);
                  if (json.isArray())
                  {
                      for (const auto &uri : json)
                          client.redirectUris.push_back(uri.asString());
                  }
              }
          }
          cb(client);
      },
      [cb](const RedisException &e) {
          LOG_ERROR << "Redis getClient error: " << e.what();
          cb(std::nullopt);
      },
      cmd.c_str()
    );
}

void RedisOAuth2Storage::validateClient(
  const std::string &clientId,
  const std::string &clientSecret,
  BoolCallback &&cb
)
{
    if (!redisClient_)
    {
        LOG_ERROR << "Redis client is not initialized!";
        cb(false);
        return;
    }
    LOG_DEBUG << "validateClient called for: " << clientId;
    if (clientSecret.empty())
    {
        std::string cmd = "EXISTS oauth2:client:" + clientId;
        redisClient_->execCommandAsync(
          [cb](const RedisResult &result) { cb(result.asInteger() == 1); },
          [cb](const RedisException &e) {
              LOG_ERROR << "Redis EXISTS error: " << e.what();
              cb(false);
          },
          cmd.c_str()
        );
    }
    else
    {
        std::string cmd = "HMGET oauth2:client:" + clientId + " secret salt";
        redisClient_->execCommandAsync(
          [cb, inputSecret = clientSecret](const RedisResult &result) {
              LOG_DEBUG << "validateClient HMGET result received";
              if (
                result.type() == RedisResultType::kNil || result.type() != RedisResultType::kArray
              )
              {
                  cb(false);
                  return;
              }
              auto arr = result.asArray();
              if (arr.size() < 2 || arr[0].type() == RedisResultType::kNil ||
                  arr[1].type() == RedisResultType::kNil)
              {
                  // HMGET returns an array of nil elements (not a top-level
                  // nil) when the hash key doesn't exist or the requested
                  // fields are missing. RedisResult::asString() throws for
                  // a kNil element, so this must be checked before calling
                  // it (bug fix: previously crashed the process here for a
                  // nonexistent client with a non-empty secret).
                  cb(false);
                  return;
              }

              std::string storedHash = arr[0].asString();
              std::string salt = arr[1].asString();
              std::string input = inputSecret + salt;
              std::string calculatedHash = drogon::utils::getSha256(input.data(), input.length());

              // Case-insensitive comparison
              std::transform(
                calculatedHash.begin(), calculatedHash.end(), calculatedHash.begin(), ::tolower
              );
              std::transform(storedHash.begin(), storedHash.end(), storedHash.begin(), ::tolower);

              LOG_DEBUG << "validateClient match result: " << (calculatedHash == storedHash);
              cb(calculatedHash == storedHash);
          },
          [cb](const RedisException &e) {
              LOG_ERROR << "Redis validateClient HMGET error: " << e.what();
              cb(false);
          },
          cmd.c_str()
        );
    }
}

void RedisOAuth2Storage::saveAuthCode(const OAuth2AuthCode &code, VoidCallback &&cb)
{
    if (!redisClient_)
    {
        if (cb)
            cb();
        return;
    }
    Json::Value val;
    val["client_id"] = code.clientId;
    val["user_id"] = code.userId;
    val["scope"] = code.scope;
    val["redirect_uri"] = code.redirectUri;
    val["expires_at"] = (Json::Int64)code.expiresAt;
    val["used"] = code.used;
    std::string jsonStr = jsonToString(val);

    auto now = std::chrono::system_clock::now();
    size_t nowSec =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    size_t ttl = (code.expiresAt > (int64_t)nowSec) ? (code.expiresAt - nowSec) : 1;

    std::string key = "oauth2:code:" + code.code;
    std::string ttlStr = std::to_string(ttl);

    LOG_DEBUG << "saveAuthCode CMD: SETEX " << key << " " << ttlStr << " " << jsonStr;

    redisClient_->execCommandAsync(
      [cb, codeStr = code.code](const RedisResult &result) {
          LOG_DEBUG << "saveAuthCode SUCCESS for: " << codeStr << " Result: " << result.asString();
          if (cb)
              cb();
      },
      [cb, codeStr = code.code](const RedisException &e) {
          LOG_ERROR << "saveAuthCode ERROR for: " << codeStr << " Error: " << e.what();
          if (cb)
              cb();
      },
      "SETEX %s %s %s",
      key.c_str(),
      ttlStr.c_str(),
      jsonStr.c_str()
    );
}

void RedisOAuth2Storage::getAuthCode(const std::string &code, AuthCodeCallback &&cb)
{
    if (!redisClient_)
    {
        cb(std::nullopt);
        return;
    }
    std::string key = "oauth2:code:" + code;
    LOG_DEBUG << "getAuthCode CMD: GET " << key;

    redisClient_->execCommandAsync(
      [cb, codeStr = code](const RedisResult &result) {
          if (result.type() == RedisResultType::kNil)
          {
              LOG_WARN << "getAuthCode: Key not found for: " << codeStr;
              cb(std::nullopt);
              return;
          }
          std::string jsonStr = result.asString();
          LOG_DEBUG << "getAuthCode Result: " << jsonStr;

          auto json = parseJson(jsonStr);
          if (json.isNull())
          {
              LOG_ERROR << "getAuthCode: Failed to parse JSON";
              cb(std::nullopt);
              return;
          }

          OAuth2AuthCode authCode;
          authCode.code = codeStr;
          authCode.clientId = json["client_id"].asString();
          authCode.userId = json["user_id"].asString();
          authCode.scope = json["scope"].asString();
          authCode.redirectUri = json["redirect_uri"].asString();
          authCode.expiresAt = json["expires_at"].asInt64();
          authCode.used = json["used"].asBool();
          cb(authCode);
      },
      [cb, codeStr = code](const RedisException &e) {
          LOG_ERROR << "getAuthCode ERROR for: " << codeStr << " Error: " << e.what();
          cb(std::nullopt);
      },
      "GET %s",
      key.c_str()
    );
}

// Mark used: We update the JSON to set used=true, preserving TTL
void RedisOAuth2Storage::markAuthCodeUsed(const std::string &code, VoidCallback &&cb)
{
    if (!redisClient_)
    {
        if (cb)
            cb();
        return;
    }
    std::string key = "oauth2:code:" + code;

    // Lua script to Atomic Set Used=true
    std::string script = R"(
        local key = KEYS[1]
        local val = redis.call('GET', key)
        if not val then return nil end
        local json = cjson.decode(val)
        json.used = true
        local newVal = cjson.encode(json)
        local ttl = redis.call('TTL', key)
        if ttl > 0 then
            redis.call('SETEX', key, ttl, newVal)
        else
            redis.call('SET', key, newVal)
        end
        return 1
    )";

    redisClient_->execCommandAsync(
      [cb](const RedisResult &) {
          if (cb)
              cb();
      },
      [cb](const RedisException &) {
          if (cb)
              cb();
      },
      "EVAL %s 1 %s",
      script.c_str(),
      key.c_str()
    );
}

void RedisOAuth2Storage::consumeAuthCode(
  const std::string &code,
  const std::string &redirectUri,
  AuthCodeCallback &&cb
)
{
    if (!redisClient_)
    {
        cb(std::nullopt);
        return;
    }
    std::string key = "oauth2:code:" + code;

    std::string script = R"(
        local key = KEYS[1]
        local redirect_uri = ARGV[1]
        local val = redis.call('GET', key)
        if not val then return nil end
        local json = cjson.decode(val)
        if json.used then return nil end
        -- CRITICAL: Validate redirect_uri matches authorization
        -- Per OAuth2 RFC 6749 Section 4.1.3
        if redirect_uri ~= "" and redirect_uri ~= json.redirect_uri then
            return nil
        end
        json.used = true
        local newVal = cjson.encode(json)
        local ttl = redis.call('TTL', key)
        if ttl > 0 then
            redis.call('SETEX', key, ttl, newVal)
        else
            redis.call('SET', key, newVal)
        end
        return newVal
    )";

    redisClient_->execCommandAsync(
      [cb, codeStr = code, requestUri = redirectUri](const RedisResult &result) {
          if (result.type() == RedisResultType::kNil)
          {
              // Log if this was a redirect_uri mismatch vs code not found
              // (we can't distinguish in Lua script, but we can log the
              // attempt)
              if (!requestUri.empty())
              {
                  LOG_DEBUG << "[SECURITY] Auth code consumption failed "
                            << "(code not found, expired, or redirect_uri "
                               "mismatch): "
                            << codeStr;
              }
              cb(std::nullopt);
              return;
          }
          std::string jsonStr = result.asString();

          auto json = parseJson(jsonStr);

          if (json.isNull())
          {
              LOG_ERROR << "consumeAuthCode: Failed to parse JSON result";
              cb(std::nullopt);
              return;
          }

          OAuth2AuthCode authCode;
          authCode.code = codeStr;
          authCode.clientId = json["client_id"].asString();
          authCode.userId = json["user_id"].asString();
          authCode.scope = json["scope"].asString();
          authCode.redirectUri = json["redirect_uri"].asString();
          authCode.expiresAt = json["expires_at"].asInt64();
          authCode.used = true;  // We just marked it

          cb(authCode);
      },
      [cb](const RedisException &e) {
          LOG_ERROR << "consumeAuthCode Redis Error: " << e.what();
          cb(std::nullopt);
      },
      "EVAL %s 1 %s %s",
      script.c_str(),
      key.c_str(),
      redirectUri.c_str()
    );
}

void RedisOAuth2Storage::saveAccessToken(const OAuth2AccessToken &token, VoidCallback &&cb)
{
    if (!redisClient_)
    {
        if (cb)
            cb();
        return;
    }
    Json::Value val;
    val["client_id"] = token.clientId;
    val["user_id"] = token.userId;
    val["scope"] = token.scope;
    val["expires_at"] = (Json::Int64)token.expiresAt;
    val["revoked"] = token.revoked;

    // P1: RFC 7662 fields
    val["issued_at"] = (Json::Int64)token.issuedAt;
    val["issuer"] = token.issuer;
    val["audience"] = token.audience;
    val["not_before"] = (Json::Int64)token.notBefore;
    val["introspect_count"] = token.introspectCount;
    val["revoked_at"] = (Json::Int64)token.revokedAt;
    val["revoked_by"] = token.revokedBy;

    std::string jsonStr = jsonToString(val);

    auto now = std::chrono::system_clock::now();
    size_t nowSec =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    size_t ttl = (token.expiresAt > (int64_t)nowSec) ? (token.expiresAt - nowSec) : 1;

    std::string key = "oauth2:token:" + token.token;
    std::string ttlStr = std::to_string(ttl);

    redisClient_->execCommandAsync(
      [cb](const RedisResult &) {
          if (cb)
              cb();
      },
      [cb](const RedisException &) {
          if (cb)
              cb();
      },
      "SETEX %s %s %s",
      key.c_str(),
      ttlStr.c_str(),
      jsonStr.c_str()
    );
}

void RedisOAuth2Storage::getAccessToken(const std::string &token, AccessTokenCallback &&cb)
{
    if (!redisClient_)
    {
        cb(std::nullopt);
        return;
    }
    std::string key = "oauth2:token:" + token;
    redisClient_->execCommandAsync(
      [cb, tokenStr = token](const RedisResult &result) {
          if (result.type() == RedisResultType::kNil)
          {
              cb(std::nullopt);
              return;
          }
          std::string jsonStr = result.asString();
          auto json = parseJson(jsonStr);
          if (json.isNull())
          {
              cb(std::nullopt);
              return;
          }
          OAuth2AccessToken accessToken;
          accessToken.token = tokenStr;
          accessToken.clientId = json["client_id"].asString();
          accessToken.userId = json["user_id"].asString();
          accessToken.scope = json["scope"].asString();
          accessToken.expiresAt = json["expires_at"].asInt64();
          accessToken.revoked = json["revoked"].asBool();

          // P1: RFC 7662 fields (with backward compatibility)
          if (json.isMember("issued_at"))
              accessToken.issuedAt = json["issued_at"].asInt64();
          if (json.isMember("issuer"))
              accessToken.issuer = json["issuer"].asString();
          if (json.isMember("audience"))
              accessToken.audience = json["audience"].asString();
          if (json.isMember("not_before"))
              accessToken.notBefore = json["not_before"].asInt64();
          if (json.isMember("introspect_count"))
              accessToken.introspectCount = json["introspect_count"].asInt();
          if (json.isMember("revoked_at"))
              accessToken.revokedAt = json["revoked_at"].asInt64();
          if (json.isMember("revoked_by"))
              accessToken.revokedBy = json["revoked_by"].asString();

          cb(accessToken);
      },
      [cb](const RedisException &) { cb(std::nullopt); },
      "GET %s",
      key.c_str()
    );
}

void RedisOAuth2Storage::saveRefreshToken(const OAuth2RefreshToken &token, VoidCallback &&cb)
{
    if (cb)
        cb();
}

void RedisOAuth2Storage::getRefreshToken(const std::string &token, RefreshTokenCallback &&cb)
{
    if (cb)
        cb(std::nullopt);
}

void RedisOAuth2Storage::revokeRefreshToken(const std::string &token, VoidCallback &&cb)
{
    if (!redisClient_)
    {
        if (cb)
            cb();
        return;
    }

    redisClient_->execCommandAsync(
      [cb](const RedisResult &r) {
          if (cb)
              cb();
      },
      [cb](const std::exception &e) {
          LOG_ERROR << "Failed to revoke refresh token in Redis: " << e.what();
          // Call callback even on failure to avoid blocking
          if (cb)
              cb();
      },
      "HSET oauth2_refresh_tokens:%s revoked 1",
      token.c_str()
    );
}

void RedisOAuth2Storage::atomicRevokeRefreshToken(
  const std::string &token,
  RefreshTokenCallback &&cb
)
{
    // Redis doesn't have native CAS, but we can use HSETNX-like logic
    // For simplicity, get then set (acceptable for Redis single-threaded model)
    getRefreshToken(
      token, [self = shared_from_this(), this, token, cb = std::move(cb)](auto rt) mutable {
          if (!rt || rt->revoked)
          {
              cb(std::nullopt);
              return;
          }
          auto captured = *rt;
          revokeRefreshToken(token, [cb = std::move(cb), captured]() { cb(captured); });
      }
    );
}

void RedisOAuth2Storage::revokeTokenFamily(const std::string &familyId, VoidCallback &&cb)
{
    // Redis doesn't support efficient family queries without secondary indexes
    // For production Redis usage, maintain a SET of tokens per family
    // For now, just log and callback (family tracking is primarily for Postgres)
    LOG_WARN << "[SECURITY] Token family revocation requested for: " << familyId
             << " (Redis: limited support)";
    if (cb)
        cb();
}

// Redis handles expiration via TTL automatically.
void RedisOAuth2Storage::deleteExpiredData()
{
    LOG_DEBUG << "Redis deleteExpiredData called (No-op, relying on Redis TTL)";
}

void RedisOAuth2Storage::getUserRoles(const std::string &userId, StringListCallback &&cb)
{
    // Default role for redis (until we implement role storage in redis)
    cb({"user"});
}

void RedisOAuth2Storage::getUserRoles(int32_t internalUserId, StringListCallback &&cb)
{
    // Default role for redis (until we implement role storage in redis)
    cb({"user"});
}

// ========== Subject Mapping Operations ==========

void RedisOAuth2Storage::getInternalUserId(
  const std::string &subject,
  const std::string &provider,
  OptionalIntCallback &&cb
)
{
    // Redis implementation using hash maps
    // Key: oauth2:subject_mapping:{provider}:{subject}
    if (!redisClient_)
    {
        cb(std::nullopt);
        return;
    }

    std::string key = "oauth2:subject_mapping:" + provider + ":" + subject;
    redisClient_->execCommandAsync(
      [cb](const RedisResult &result) {
          if (result.type() == RedisResultType::kNil)
          {
              cb(std::nullopt);
              return;
          }
          // Redis HGET returns string value or nil
          std::string userIdStr = result.asString();
          try
          {
              int32_t userId = std::stoi(userIdStr);
              cb(userId);
          }
          catch (...)
          {
              LOG_ERROR << "Failed to parse user ID from Redis: " << userIdStr;
              cb(std::nullopt);
          }
      },
      [cb](const RedisException &e) {
          LOG_ERROR << "Redis getInternalUserId error: " << e.what();
          cb(std::nullopt);
      },
      "HGET %s user_id",
      key.c_str()
    );
}

void RedisOAuth2Storage::createSubjectMapping(
  const std::string &subject,
  int32_t internalUserId,
  const std::string &provider,
  BoolCallback &&cb
)
{
    if (!redisClient_)
    {
        cb(false);
        return;
    }

    std::string key = "oauth2:subject_mapping:" + provider + ":" + subject;
    std::string userIdStr = std::to_string(internalUserId);

    redisClient_->execCommandAsync(
      [cb](const RedisResult &result) {
          // HSET returns 1 for new field, 0 for updated field
          cb(true);
      },
      [cb, subject, provider](const RedisException &e) {
          LOG_ERROR << "Failed to create subject mapping in Redis: " << e.what();
          cb(false);
      },
      "HSET %s user_id %s",
      key.c_str(),
      userIdStr.c_str()
    );
}

// ========== Authorization Transaction Operations ==========

void RedisOAuth2Storage::saveAuthorizationTransaction(
  const AuthorizationTransaction &transaction,
  BoolCallback &&cb
)
{
    if (!redisClient_)
    {
        cb(false);
        return;
    }

    std::string key = "oauth2:transaction:" + transaction.transactionId;
    Json::Value val;
    val["transaction_id"] = transaction.transactionId;
    val["client_id"] = transaction.clientId;
    val["subject"] = transaction.subject;
    val["redirect_uri"] = transaction.redirectUri;
    val["state"] = transaction.state;
    val["code_challenge"] = transaction.codeChallenge;
    val["code_challenge_method"] = transaction.codeChallengeMethod;
    val["consumed"] = transaction.consumed;
    val["expires_at"] = (Json::Int64)transaction.expiresAt;

    // Serialize requested scopes
    Json::Value scopesJson(Json::arrayValue);
    for (const auto &scope : transaction.requestedScopes)
        scopesJson.append(scope);
    val["requested_scopes"] = scopesJson;

    // Serialize valid scopes
    Json::Value validScopesJson(Json::arrayValue);
    for (const auto &scope : transaction.validScopes)
        validScopesJson.append(scope);
    val["valid_scopes"] = validScopesJson;

    // Serialize consent required scopes
    Json::Value consentScopesJson(Json::arrayValue);
    for (const auto &scope : transaction.consentRequiredScopes)
        consentScopesJson.append(scope);
    val["consent_required_scopes"] = consentScopesJson;

    std::string jsonStr = jsonToString(val);

    auto now = std::chrono::system_clock::now();
    size_t nowSec =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    size_t ttl = (transaction.expiresAt > (int64_t)nowSec) ? (transaction.expiresAt - nowSec) : 600;

    redisClient_->execCommandAsync(
      [cb](const RedisResult &) { cb(true); },
      [cb](const RedisException &e) {
          LOG_ERROR << "Failed to save authorization transaction: " << e.what();
          cb(false);
      },
      "SETEX %s %d %s",
      key.c_str(),
      ttl,
      jsonStr.c_str()
    );
}

void RedisOAuth2Storage::getAuthorizationTransaction(
  const std::string &transactionId,
  TransactionCallback &&cb
)
{
    if (!redisClient_)
    {
        cb(std::nullopt);
        return;
    }

    std::string key = "oauth2:transaction:" + transactionId;
    redisClient_->execCommandAsync(
      [cb](const RedisResult &result) {
          if (result.type() == RedisResultType::kNil)
          {
              cb(std::nullopt);
              return;
          }

          std::string jsonStr = result.asString();
          auto json = parseJson(jsonStr);
          if (json.isNull())
          {
              cb(std::nullopt);
              return;
          }

          AuthorizationTransaction transaction;
          transaction.transactionId = json["transaction_id"].asString();
          transaction.clientId = json["client_id"].asString();
          transaction.subject = json["subject"].asString();
          transaction.redirectUri = json["redirect_uri"].asString();
          transaction.state = json["state"].asString();
          transaction.codeChallenge = json["code_challenge"].asString();
          transaction.codeChallengeMethod = json["code_challenge_method"].asString();
          transaction.consumed = json["consumed"].asBool();
          transaction.expiresAt = json["expires_at"].asInt64();

          // Parse requested scopes
          if (json.isMember("requested_scopes") && json["requested_scopes"].isArray())
          {
              for (const auto &scope : json["requested_scopes"])
                  transaction.requestedScopes.push_back(scope.asString());
          }

          // Parse valid scopes
          if (json.isMember("valid_scopes") && json["valid_scopes"].isArray())
          {
              for (const auto &scope : json["valid_scopes"])
                  transaction.validScopes.push_back(scope.asString());
          }

          // Parse consent required scopes
          if (json.isMember("consent_required_scopes") && json["consent_required_scopes"].isArray())
          {
              for (const auto &scope : json["consent_required_scopes"])
                  transaction.consentRequiredScopes.push_back(scope.asString());
          }

          cb(transaction);
      },
      [cb](const RedisException &e) {
          LOG_ERROR << "Failed to get authorization transaction: " << e.what();
          cb(std::nullopt);
      },
      "GET %s",
      key.c_str()
    );
}

void RedisOAuth2Storage::deleteAuthorizationTransaction(
  const std::string &transactionId,
  VoidCallback &&cb
)
{
    if (!redisClient_)
    {
        if (cb)
            cb();
        return;
    }

    std::string key = "oauth2:transaction:" + transactionId;
    redisClient_->execCommandAsync(
      [cb](const RedisResult &) {
          if (cb)
              cb();
      },
      [cb](const RedisException &) {
          if (cb)
              cb();
      },
      "DEL %s",
      key.c_str()
    );
}

void RedisOAuth2Storage::markTransactionConsumed(
  const std::string &transactionId,
  BoolCallback &&cb
)
{
    if (!redisClient_)
    {
        cb(false);
        return;
    }

    std::string script = R"(
        local key = KEYS[1]
        local val = redis.call('GET', key)
        if not val then return 0 end
        local json = cjson.decode(val)
        if json.consumed then return 0 end
        json.consumed = true
        local newVal = cjson.encode(json)
        redis.call('SETEX', key, redis.call('TTL', key), newVal)
        return 1
    )";

    redisClient_->execCommandAsync(
      [cb](const RedisResult &result) {
          // Script returns 1 if marked successfully, 0 if already consumed or
          // not found
          cb(result.asInteger() == 1);
      },
      [cb](const RedisException &e) {
          LOG_ERROR << "Failed to mark transaction as consumed: " << e.what();
          cb(false);
      },
      "EVAL %s 1 %s",
      script.c_str(),
      transactionId.c_str()
    );
}

// ========== Scope Management Operations ==========

void RedisOAuth2Storage::hasUserConsent(
  int32_t internalUserId,
  const std::string &clientId,
  const std::string &scope,
  BoolCallback &&cb
)
{
    if (!redisClient_)
    {
        cb(false);
        return;
    }

    std::string key =
      "oauth2:consent:" + std::to_string(internalUserId) + ":" + clientId + ":" + scope;
    redisClient_->execCommandAsync(
      [cb](const RedisResult &result) {
          // EXISTS returns an integer reply (1 if the key exists, 0
          // otherwise), never a nil reply -- so the previous
          // `type() != kNil` check was always true regardless of whether
          // the key existed (bug fix: hasUserConsent always returned true).
          cb(result.type() == RedisResultType::kInteger && result.asInteger() == 1);
      },
      [cb](const RedisException &e) {
          LOG_ERROR << "Redis hasUserConsent error: " << e.what();
          cb(false);
      },
      "EXISTS %s",
      key.c_str()
    );
}

void RedisOAuth2Storage::saveUserConsent(
  int32_t internalUserId,
  const std::string &clientId,
  const std::string &scope,
  BoolCallback &&cb
)
{
    if (!redisClient_)
    {
        cb(false);
        return;
    }

    std::string key =
      "oauth2:consent:" + std::to_string(internalUserId) + ":" + clientId + ":" + scope;
    auto now = std::chrono::system_clock::now();
    size_t nowSec =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    size_t ttl = 30 * 24 * 3600;  // 30 days

    redisClient_->execCommandAsync(
      [cb](const RedisResult &) { cb(true); },
      [cb](const RedisException &e) {
          LOG_ERROR << "Failed to save user consent: " << e.what();
          cb(false);
      },
      "SETEX %s %d %d",
      key.c_str(),
      ttl,
      nowSec
    );
}

void RedisOAuth2Storage::revokeUserConsent(
  int32_t internalUserId,
  const std::string &clientId,
  const std::string &scope,
  VoidCallback &&cb
)
{
    if (!redisClient_)
    {
        if (cb)
            cb();
        return;
    }

    std::string key =
      "oauth2:consent:" + std::to_string(internalUserId) + ":" + clientId + ":" + scope;
    redisClient_->execCommandAsync(
      [cb](const RedisResult &) {
          if (cb)
              cb();
      },
      [cb](const RedisException &) {
          if (cb)
              cb();
      },
      "DEL %s",
      key.c_str()
    );
}

// ========== P1: Token Introspection (RFC 7662) ==========

void RedisOAuth2Storage::introspectToken(
  const std::string &token,
  IOAuth2Storage::TokenIntrospectionCallback &&cb
)
{
    if (!redisClient_)
    {
        TokenIntrospection introspection;
        introspection.active = false;
        cb(introspection);
        return;
    }

    std::string key = "oauth2:token:" + token;
    redisClient_->execCommandAsync(
      [cb](const RedisResult &result) {
          if (result.type() == RedisResultType::kNil)
          {
              TokenIntrospection introspection;
              introspection.active = false;
              cb(introspection);
              return;
          }

          std::string jsonStr = result.asString();
          auto json = parseJson(jsonStr);
          if (json.isNull())
          {
              TokenIntrospection introspection;
              introspection.active = false;
              cb(introspection);
              return;
          }

          // Check if token is revoked or expired
          bool revoked = json["revoked"].asBool();
          int64_t expiresAt = json["expires_at"].asInt64();
          int64_t now = std::time(nullptr);

          if (revoked || expiresAt < now)
          {
              TokenIntrospection introspection;
              introspection.active = false;
              cb(introspection);
              return;
          }

          // Token is active, populate introspection data
          TokenIntrospection introspection;
          introspection.active = true;
          introspection.clientId = json["client_id"].asString();
          introspection.tokenType = "Bearer";
          introspection.exp = expiresAt;

          // P1 fields (with backward compatibility)
          if (json.isMember("issued_at"))
              introspection.iat = json["issued_at"].asInt64();
          else
              introspection.iat = now;

          if (json.isMember("issuer"))
              introspection.iss = json["issuer"].asString();
          else
              introspection.iss = "https://oauth.example.com";

          if (json.isMember("audience"))
              introspection.aud = json["audience"].asString();

          if (json.isMember("not_before"))
              introspection.nbf = json["not_before"].asInt64();
          else
              introspection.nbf = now;

          introspection.sub = json["user_id"].asString();
          introspection.scope = json["scope"].asString();

          cb(introspection);
      },
      [cb](const RedisException &) {
          TokenIntrospection introspection;
          introspection.active = false;
          cb(introspection);
      },
      "GET %s",
      key.c_str()
    );
}

void RedisOAuth2Storage::incrementIntrospectCount(
  const std::string &token,
  IOAuth2Storage::VoidCallback &&cb
)
{
    if (!redisClient_)
    {
        if (cb)
            cb();
        return;
    }

    std::string key = "oauth2:token:" + token;
    redisClient_->execCommandAsync(
      [cb](const RedisResult &result) {
          // Note: Redis doesn't have atomic increment for JSON fields
          // We need to get the JSON, update the field, and set it back
          // For now, this is a no-op in Redis storage to avoid race conditions
          // The introspect_count is mainly for monitoring in PostgreSQL
          if (cb)
              cb();
      },
      [cb](const RedisException &) {
          if (cb)
              cb();
      },
      "GET %s",
      key.c_str()
    );
}

// ========== P1: Token Revocation (RFC 7009) ==========

void RedisOAuth2Storage::revokeAccessToken(
  const std::string &token,
  const std::string &revokedBy,
  IOAuth2Storage::VoidCallback &&cb
)
{
    if (!redisClient_)
    {
        if (cb)
            cb();
        return;
    }

    std::string key = "oauth2:token:" + token;
    redisClient_->execCommandAsync(
      [self = shared_from_this(), this, cb, key, revokedBy](const RedisResult &result) {
          if (result.type() == RedisResultType::kNil)
          {
              // Token doesn't exist, but return success per RFC 7009
              if (cb)
                  cb();
              return;
          }

          // Token exists, revoke it by updating JSON
          std::string jsonStr = result.asString();
          auto json = parseJson(jsonStr);
          if (!json.isNull())
          {
              json["revoked"] = true;
              json["revoked_at"] = (Json::Int64)std::time(nullptr);
              json["revoked_by"] = revokedBy;

              std::string updatedJsonStr = jsonToString(json);

              // Update the token with revoked status
              // Note: This is not atomic, but acceptable for Redis cache
              redisClient_->execCommandAsync(
                [cb](const RedisResult &) {
                    LOG_INFO << "Token revoked successfully in Redis";
                    if (cb)
                        cb();
                },
                [cb](const RedisException &) {
                    LOG_ERROR << "Failed to update revoked token in Redis";
                    if (cb)
                        cb();
                },
                "SETEX %s %s %s",
                key.c_str(),
                "3600",  // Keep for 1 hour (cleanup will handle it)
                updatedJsonStr.c_str()
              );
          }
          else
          {
              if (cb)
                  cb();
          }
      },
      [cb](const RedisException &) {
          // Token doesn't exist or Redis error
          // Return success per RFC 7009 (prevent token probing)
          if (cb)
              cb();
      },
      "GET %s",
      key.c_str()
    );
}

void RedisOAuth2Storage::getUserInfo(const std::string &userId, OptionalJsonCallback &&cb)
{
    // Redis storage doesn't maintain user details
    // Return nullopt to indicate user info not available
    cb(std::nullopt);
}

void RedisOAuth2Storage::getUserInfo(int32_t internalUserId, OptionalJsonCallback &&cb)
{
    // Redis storage doesn't maintain user details
    // Return nullopt to indicate user info not available
    cb(std::nullopt);
}

}  // namespace oauth2
