#include "RedisOAuth2Storage.h"
#include <drogon/utils/Utilities.h>
#include <json/json.h>
#include <sstream>
#include <algorithm>
#include "plugins/OAuth2Metrics.h"

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

std::unique_ptr<IOAuth2Storage> createRedisStorage(const Json::Value &config)
{
    std::string clientName = config.get("client_name", "default").asString();
    return std::make_unique<RedisOAuth2Storage>(clientName);
}

void RedisOAuth2Storage::getClient(const std::string &clientId,
                                   ClientCallback &&cb)
{
    if (!redisClient_)
    {
        LOG_ERROR << "Redis client is not initialized!";
        cb(std::nullopt);
        return;
    }
    std::string cmd = "HGETALL oauth2:client:" + clientId;
    auto timer = std::make_shared<OperationTimer>("getClient", "redis");
    redisClient_->execCommandAsync(
        [cb, clientId, timer](const RedisResult &result) {
            if (result.type() == RedisResultType::kNil ||
                result.type() != RedisResultType::kArray)
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
        cmd.c_str());
}

void RedisOAuth2Storage::validateClient(const std::string &clientId,
                                        const std::string &clientSecret,
                                        BoolCallback &&cb)
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
            cmd.c_str());
    }
    else
    {
        std::string cmd = "HMGET oauth2:client:" + clientId + " secret salt";
        redisClient_->execCommandAsync(
            [cb, inputSecret = clientSecret](const RedisResult &result) {
                LOG_DEBUG << "validateClient HMGET result received";
                if (result.type() == RedisResultType::kNil ||
                    result.type() != RedisResultType::kArray)
                {
                    cb(false);
                    return;
                }
                auto arr = result.asArray();
                if (arr.size() < 2)
                {
                    cb(false);
                    return;
                }

                std::string storedHash = arr[0].asString();
                std::string salt = arr[1].asString();
                std::string input = inputSecret + salt;
                std::string calculatedHash =
                    drogon::utils::getSha256(input.data(), input.length());

                // Case-insensitive comparison
                std::transform(calculatedHash.begin(),
                               calculatedHash.end(),
                               calculatedHash.begin(),
                               ::tolower);
                std::transform(storedHash.begin(),
                               storedHash.end(),
                               storedHash.begin(),
                               ::tolower);

                LOG_DEBUG << "validateClient match result: "
                          << (calculatedHash == storedHash);
                cb(calculatedHash == storedHash);
            },
            [cb](const RedisException &e) {
                LOG_ERROR << "Redis validateClient HMGET error: " << e.what();
                cb(false);
            },
            cmd.c_str());
    }
}

void RedisOAuth2Storage::saveAuthCode(const OAuth2AuthCode &code,
                                      VoidCallback &&cb)
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
    Json::FastWriter writer;
    std::string jsonStr = writer.write(val);

    auto now = std::chrono::system_clock::now();
    size_t nowSec =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
            .count();
    size_t ttl =
        (code.expiresAt > (int64_t)nowSec) ? (code.expiresAt - nowSec) : 1;

    std::string key = "oauth2:code:" + code.code;
    std::string ttlStr = std::to_string(ttl);

    LOG_DEBUG << "saveAuthCode CMD: SETEX " << key << " " << ttlStr << " "
              << jsonStr;

    redisClient_->execCommandAsync(
        [cb, codeStr = code.code](const RedisResult &result) {
            LOG_DEBUG << "saveAuthCode SUCCESS for: " << codeStr
                      << " Result: " << result.asString();
            if (cb)
                cb();
        },
        [cb, codeStr = code.code](const RedisException &e) {
            LOG_ERROR << "saveAuthCode ERROR for: " << codeStr
                      << " Error: " << e.what();
            if (cb)
                cb();
        },
        "SETEX %s %s %s",
        key.c_str(),
        ttlStr.c_str(),
        jsonStr.c_str());
}

void RedisOAuth2Storage::getAuthCode(const std::string &code,
                                     AuthCodeCallback &&cb)
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
            LOG_ERROR << "getAuthCode ERROR for: " << codeStr
                      << " Error: " << e.what();
            cb(std::nullopt);
        },
        "GET %s",
        key.c_str());
}

// Mark used: We update the JSON to set used=true, preserving TTL
void RedisOAuth2Storage::markAuthCodeUsed(const std::string &code,
                                          VoidCallback &&cb)
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
        key.c_str());
}

void RedisOAuth2Storage::consumeAuthCode(const std::string &code,
                                         AuthCodeCallback &&cb)
{
    if (!redisClient_)
    {
        cb(std::nullopt);
        return;
    }
    std::string key = "oauth2:code:" + code;

    std::string script = R"(
        local key = KEYS[1]
        local val = redis.call('GET', key)
        if not val then return nil end
        local json = cjson.decode(val)
        if json.used then return nil end
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
        [cb, codeStr = code](const RedisResult &result) {
            if (result.type() == RedisResultType::kNil)
            {
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
        "EVAL %s 1 %s",
        script.c_str(),
        key.c_str());
}

void RedisOAuth2Storage::saveAccessToken(const OAuth2AccessToken &token,
                                         VoidCallback &&cb)
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
    Json::FastWriter writer;
    std::string jsonStr = writer.write(val);

    auto now = std::chrono::system_clock::now();
    size_t nowSec =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
            .count();
    size_t ttl =
        (token.expiresAt > (int64_t)nowSec) ? (token.expiresAt - nowSec) : 1;

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
        jsonStr.c_str());
}

void RedisOAuth2Storage::getAccessToken(const std::string &token,
                                        AccessTokenCallback &&cb)
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
            cb(accessToken);
        },
        [cb](const RedisException &) { cb(std::nullopt); },
        "GET %s",
        key.c_str());
}

void RedisOAuth2Storage::saveRefreshToken(const OAuth2RefreshToken &token,
                                          VoidCallback &&cb)
{
    if (cb)
        cb();
}

void RedisOAuth2Storage::getRefreshToken(const std::string &token,
                                         RefreshTokenCallback &&cb)
{
    if (cb)
        cb(std::nullopt);
}

// Redis handles expiration via TTL automatically.
void RedisOAuth2Storage::deleteExpiredData()
{
    LOG_DEBUG << "Redis deleteExpiredData called (No-op, relying on Redis TTL)";
}

}  // namespace oauth2
