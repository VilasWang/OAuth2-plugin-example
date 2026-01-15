#include "RedisOAuth2Storage.h"
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <drogon/nosql/RedisResult.h>
#include <chrono>

namespace oauth2 {

RedisOAuth2Storage::RedisOAuth2Storage(const std::string& redisClientName)
    : redisClientName_(redisClientName)
{
}

void RedisOAuth2Storage::initFromConfig(const Json::Value& config)
{
    if (config.isMember("client_name")) {
        redisClientName_ = config["client_name"].asString();
    }
    LOG_INFO << "RedisOAuth2Storage initialized with Client: " << redisClientName_;
}

drogon::nosql::RedisClientPtr RedisOAuth2Storage::getRedisClient() const
{
    return drogon::app().getRedisClient(redisClientName_);
}

std::string RedisOAuth2Storage::getClientKey(const std::string& clientId) const {
    return "oauth2:client:" + clientId;
}

std::string RedisOAuth2Storage::getAuthCodeKey(const std::string& code) const {
    return "oauth2:code:" + code;
}

std::string RedisOAuth2Storage::getAccessTokenKey(const std::string& token) const {
    return "oauth2:token:" + token;
}

std::string RedisOAuth2Storage::getRefreshTokenKey(const std::string& token) const {
    return "oauth2:refresh:" + token;
}

bool RedisOAuth2Storage::validateClient(const std::string& clientId, const std::string& clientSecret)
{
    auto client = getClient(clientId);
    if (!client) return false;
    
    // If clientSecret is empty, only check if client exists (used in /authorize phase)
    // If clientSecret is provided, verify it matches (used in /token phase)
    if (clientSecret.empty()) {
        return true;
    }
    
    // Hash the input secret with salt
    std::string input = clientSecret + client->salt;
    std::string calculatedHash = drogon::utils::getSha256(input.data(), input.length());
    
    // Normalize to lowercase for comparison (stored hash depends on generator)
    std::transform(calculatedHash.begin(), calculatedHash.end(), calculatedHash.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    std::string storedHash = client->clientSecretHash;
    std::transform(storedHash.begin(), storedHash.end(), storedHash.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    return storedHash == calculatedHash;
}

void RedisOAuth2Storage::cleanupExpiredAuthCodes()
{
    // Redis handles expiration automatically via TTL.
}

void RedisOAuth2Storage::revokeAllUserTokens(const std::string& userId)
{
    LOG_WARN << "revokeAllUserTokens not fully implemented for Redis backend";
}

void RedisOAuth2Storage::saveRefreshToken(const OAuth2RefreshToken& token)
{
    auto redis = getRedisClient();
    if (!redis) return;

    Json::Value json;
    json["token"] = token.token;
    json["access_token"] = token.accessToken;
    json["client_id"] = token.clientId;
    json["user_id"] = token.userId;
    json["scope"] = token.scope;
    json["expires_at"] = (Json::Int64)token.expiresAt;
    json["revoked"] = token.revoked;
    
    Json::FastWriter writer;
    std::string value = writer.write(json);
    
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    int64_t ttl = token.expiresAt - now;
    if (ttl <= 0) ttl = 1;

    try {
        redis->execCommandSync<std::string>(
            [](const drogon::nosql::RedisResult &r) { return ""; },
            "SETEX %s %d %s", getRefreshTokenKey(token.token).c_str(), (int)ttl, value.c_str()
        );
        LOG_DEBUG << "Saved Refresh Token: " << token.token;
    } catch (const std::exception& e) {
        LOG_ERROR << "Redis error saving refresh token: " << e.what();
    }
}

std::optional<OAuth2RefreshToken> RedisOAuth2Storage::getRefreshToken(const std::string& token)
{
    auto redis = getRedisClient();
    if (!redis) return std::nullopt;

    try {
        auto result = redis->execCommandSync<std::string>(
            [](const drogon::nosql::RedisResult &r) -> std::string {
                if (r.isNil()) return "";
                return r.asString();
            },
            "GET %s", getRefreshTokenKey(token).c_str()
        );

        if (result.empty()) return std::nullopt;

        Json::Value json;
        Json::Reader reader;
        if (!reader.parse(result, json)) return std::nullopt;

        OAuth2RefreshToken refreshToken;
        refreshToken.token = token;
        refreshToken.accessToken = json["access_token"].asString();
        refreshToken.clientId = json["client_id"].asString();
        refreshToken.userId = json["user_id"].asString();
        refreshToken.scope = json["scope"].asString();
        refreshToken.expiresAt = json["expires_at"].asInt64();
        refreshToken.revoked = json["revoked"].asBool();

        return refreshToken;
    } catch (const drogon::nosql::RedisException& e) {
        LOG_ERROR << "Redis error in getRefreshToken: " << e.what();
        return std::nullopt;
    }
}

void RedisOAuth2Storage::revokeRefreshToken(const std::string& token)
{
    auto redis = getRedisClient();
    if (!redis) return;
    try {
        redis->execCommandSync<std::string>(
            [](const drogon::nosql::RedisResult &r) { return ""; },
            "DEL %s", getRefreshTokenKey(token).c_str()
        );
    } catch (const std::exception& e) {
        LOG_ERROR << "Redis error revoking refresh token: " << e.what();
    }
}

std::optional<OAuth2Client> RedisOAuth2Storage::getClient(const std::string& clientId)
{
    auto redis = getRedisClient();
    if (!redis) {
        LOG_ERROR << "Redis client not available";
        return std::nullopt;
    }

    try {
        // Client stored as Hash, use HGETALL
        auto key = getClientKey(clientId);
        auto arr = redis->execCommandSync<std::vector<std::string>>(
            [](const drogon::nosql::RedisResult &r) -> std::vector<std::string> {
                std::vector<std::string> result;
                if (r.isNil()) return result;
                auto arr = r.asArray();
                for (const auto& item : arr) {
                    result.push_back(item.asString());
                }
                return result;
            },
            "HGETALL %s", key.c_str()
        );

        if (arr.empty()) {
            return std::nullopt;
        }

        // Parse as key-value pairs
        std::unordered_map<std::string, std::string> hashMap;
        for (size_t i = 0; i + 1 < arr.size(); i += 2) {
            hashMap[arr[i]] = arr[i + 1];
        }

        if (hashMap.empty()) {
            return std::nullopt;
        }

        OAuth2Client client;
        client.clientId = clientId;
        client.clientSecretHash = hashMap["secret"];
        client.salt = hashMap["salt"];
        
        Json::Value uris;
        Json::Reader reader;
        if (reader.parse(hashMap["redirect_uris"], uris)) {
            for (const auto& uri : uris) {
                client.redirectUris.push_back(uri.asString());
            }
        }
        
        Json::Value scopes;
        if (reader.parse(hashMap["allowed_scopes"], scopes)) {
            for (const auto& scope : scopes) {
                client.allowedScopes.push_back(scope.asString());
            }
        }

        return client;
    } catch (const drogon::nosql::RedisException& e) {
        LOG_ERROR << "Redis error in getClient: " << e.what();
        return std::nullopt;
    }
}

void RedisOAuth2Storage::saveAuthCode(const OAuth2AuthCode& code)
{
    auto redis = getRedisClient();
    if (!redis) return;

    Json::Value json;
    json["client_id"] = code.clientId;
    json["user_id"] = code.userId;
    json["scope"] = code.scope;
    json["redirect_uri"] = code.redirectUri;
    json["code_challenge"] = code.codeChallenge;
    json["code_challenge_method"] = code.codeChallengeMethod;
    json["expires_at"] = (Json::Int64)code.expiresAt;
    json["used"] = code.used;
    
    Json::FastWriter writer;
    std::string value = writer.write(json);
    
    // Set with TTL (10 minutes = 600s)
    try {
        redis->execCommandSync<std::string>(
            [](const drogon::nosql::RedisResult &r) { return ""; },
            "SETEX %s %d %s", getAuthCodeKey(code.code).c_str(), 600, value.c_str()
        );
        LOG_DEBUG << "Saved Auth Code: " << code.code << " for client " << code.clientId;
    } catch (const std::exception& e) {
        LOG_ERROR << "Redis error saving auth code: " << e.what();
    }
}

std::optional<OAuth2AuthCode> RedisOAuth2Storage::getAuthCode(const std::string& code)
{
    auto redis = getRedisClient();
    if (!redis) return std::nullopt;

    try {
        auto result = redis->execCommandSync<std::string>(
            [](const drogon::nosql::RedisResult &r) -> std::string {
                if (r.isNil()) return "";
                return r.asString();
            },
            "GET %s", getAuthCodeKey(code).c_str()
        );

        if (result.empty()) {
            return std::nullopt;
        }

        Json::Value json;
        Json::Reader reader;
        if (!reader.parse(result, json)) {
            LOG_ERROR << "Failed to parse AuthCode JSON from Redis";
            return std::nullopt;
        }

        OAuth2AuthCode authCode;
        authCode.code = code;
        authCode.clientId = json["client_id"].asString();
        authCode.userId = json["user_id"].asString();
        authCode.scope = json["scope"].asString();
        authCode.redirectUri = json["redirect_uri"].asString();
        authCode.codeChallenge = json["code_challenge"].asString();
        authCode.codeChallengeMethod = json["code_challenge_method"].asString();
        authCode.expiresAt = json["expires_at"].asInt64();
        authCode.used = json["used"].asBool();

        return authCode;
    } catch (const drogon::nosql::RedisException& e) {
        LOG_ERROR << "Redis error in getAuthCode: " << e.what();
        return std::nullopt;
    }
}

void RedisOAuth2Storage::markAuthCodeUsed(const std::string& code)
{
    auto redis = getRedisClient();
    if (!redis) return;

    auto authCode = getAuthCode(code);
    if (authCode) {
        authCode->used = true;
        
        Json::Value json;
        json["client_id"] = authCode->clientId;
        json["user_id"] = authCode->userId;
        json["scope"] = authCode->scope;
        json["redirect_uri"] = authCode->redirectUri;
        json["code_challenge"] = authCode->codeChallenge;
        json["code_challenge_method"] = authCode->codeChallengeMethod;
        json["expires_at"] = (Json::Int64)authCode->expiresAt;
        json["used"] = true;

        Json::FastWriter writer;
        std::string value = writer.write(json);
        
        // Calculate remaining TTL
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        int64_t ttl = authCode->expiresAt - now;
        if (ttl <= 0) ttl = 1;

        try {
            redis->execCommandSync<std::string>(
                [](const drogon::nosql::RedisResult &r) { return ""; },
                "SETEX %s %d %s", getAuthCodeKey(code).c_str(), (int)ttl, value.c_str()
            );
        } catch (const std::exception& e) {
            LOG_ERROR << "Redis error marking auth code used: " << e.what();
        }
    }
}

void RedisOAuth2Storage::saveAccessToken(const OAuth2AccessToken& token)
{
    auto redis = getRedisClient();
    if (!redis) return;

    Json::Value json;
    json["client_id"] = token.clientId;
    json["user_id"] = token.userId;
    json["scope"] = token.scope;
    json["expires_at"] = (Json::Int64)token.expiresAt;
    json["revoked"] = token.revoked;
    
    Json::FastWriter writer;
    std::string value = writer.write(json);
    
    // Set with TTL (1 hour = 3600s)
    try {
        redis->execCommandSync<std::string>(
            [](const drogon::nosql::RedisResult &r) { return ""; },
            "SETEX %s %d %s", getAccessTokenKey(token.token).c_str(), 3600, value.c_str()
        );
        LOG_DEBUG << "Saved Access Token: " << token.token;
    } catch (const std::exception& e) {
        LOG_ERROR << "Redis error saving access token: " << e.what();
    }
}

std::optional<OAuth2AccessToken> RedisOAuth2Storage::getAccessToken(const std::string& token)
{
    auto redis = getRedisClient();
    if (!redis) return std::nullopt;

    try {
        auto result = redis->execCommandSync<std::string>(
            [](const drogon::nosql::RedisResult &r) -> std::string {
                if (r.isNil()) return "";
                return r.asString();
            },
            "GET %s", getAccessTokenKey(token).c_str()
        );

        if (result.empty()) {
            return std::nullopt;
        }

        Json::Value json;
        Json::Reader reader;
        if (!reader.parse(result, json)) {
            return std::nullopt;
        }

        OAuth2AccessToken accessToken;
        accessToken.token = token;
        accessToken.clientId = json["client_id"].asString();
        accessToken.userId = json["user_id"].asString();
        accessToken.scope = json["scope"].asString();
        accessToken.expiresAt = json["expires_at"].asInt64();
        accessToken.revoked = json["revoked"].asBool();

        return accessToken;
    } catch (const drogon::nosql::RedisException& e) {
        LOG_ERROR << "Redis error in getAccessToken: " << e.what();
        return std::nullopt;
    }
}

void RedisOAuth2Storage::revokeAccessToken(const std::string& token)
{
    auto redis = getRedisClient();
    if (!redis) return;

    redis->execCommandSync<std::string>(
        [](const drogon::nosql::RedisResult &r) { return ""; },
        "DEL %s", getAccessTokenKey(token).c_str()
    );
}

std::unique_ptr<IOAuth2Storage> createRedisStorage(const Json::Value& config)
{
    auto storage = std::make_unique<RedisOAuth2Storage>();
    storage->initFromConfig(config);
    return storage;
}

} // namespace oauth2
