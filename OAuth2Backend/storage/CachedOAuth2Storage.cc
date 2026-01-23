#include "CachedOAuth2Storage.h"
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>

namespace oauth2
{

CachedOAuth2Storage::CachedOAuth2Storage(
    std::unique_ptr<IOAuth2Storage> impl,
    drogon::nosql::RedisClientPtr redisClient)
    : impl_(std::move(impl)), redisClient_(std::move(redisClient))
{
}

void CachedOAuth2Storage::getClient(const std::string &clientId,
                                    ClientCallback &&cb)
{
    impl_->getClient(clientId, std::move(cb));
}

void CachedOAuth2Storage::validateClient(const std::string &clientId,
                                         const std::string &clientSecret,
                                         BoolCallback &&cb)
{
    impl_->validateClient(clientId, clientSecret, std::move(cb));
}

void CachedOAuth2Storage::saveAuthCode(const OAuth2AuthCode &code,
                                       VoidCallback &&cb)
{
    impl_->saveAuthCode(code, std::move(cb));
}

void CachedOAuth2Storage::getAuthCode(const std::string &code,
                                      AuthCodeCallback &&cb)
{
    impl_->getAuthCode(code, std::move(cb));
}

void CachedOAuth2Storage::markAuthCodeUsed(const std::string &code,
                                           VoidCallback &&cb)
{
    impl_->markAuthCodeUsed(code, std::move(cb));
}

void CachedOAuth2Storage::consumeAuthCode(const std::string &code,
                                          AuthCodeCallback &&cb)
{
    impl_->consumeAuthCode(code, std::move(cb));
}

// Access Token - Write Side (Cache Invalidation or Write-Through)
void CachedOAuth2Storage::saveAccessToken(const OAuth2AccessToken &token,
                                          VoidCallback &&cb)
{
    // Write to DB first
    impl_->saveAccessToken(token, [this, token, cb = std::move(cb)]() mutable {
        if (!redisClient_)
        {
            if (cb)
                cb();
            return;
        }

        // Write-Through to Redis
        Json::Value json;
        json["token"] = token.token;
        json["client_id"] = token.clientId;
        json["user_id"] = token.userId;
        json["scope"] = token.scope;
        json["expires_at"] = (Json::Int64)token.expiresAt;
        json["revoked"] = token.revoked;

        // Calculate TTL
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
        long ttl = token.expiresAt - now;
        if (ttl <= 0)
            ttl = 1;

        std::string key = "oauth2:token:" + token.token;
        redisClient_->execCommandAsync(
            [cb](const drogon::nosql::RedisResult &r) {
                if (cb)
                    cb();
            },
            [cb](const std::exception &e) {
                LOG_ERROR << "Redis Write Error: " << e.what();
                if (cb)
                    cb();
            },
            "SET %s %s EX %d",
            key.c_str(),
            json.toStyledString().c_str(),
            ttl);
    });
}

// Access Token - Read Side (Cache Look-Aside)
void CachedOAuth2Storage::getAccessToken(const std::string &token,
                                         AccessTokenCallback &&cb)
{
    if (!redisClient_)
    {
        impl_->getAccessToken(token, std::move(cb));
        return;
    }

    std::string key = "oauth2:token:" + token;
    auto sharedCb = std::make_shared<AccessTokenCallback>(std::move(cb));

    redisClient_->execCommandAsync(
        [this, token, sharedCb](const drogon::nosql::RedisResult &r) {
            if (r.type() == drogon::nosql::RedisResultType::kNil)
            {
                // Cache Miss -> Load from DB
                impl_->getAccessToken(
                    token,
                    [this, token, sharedCb](
                        const std::optional<OAuth2AccessToken> &optToken) {
                        if (optToken)
                        {
                            // Cache Fill
                            Json::Value json;
                            json["token"] = optToken->token;
                            json["client_id"] = optToken->clientId;
                            json["user_id"] = optToken->userId;
                            json["scope"] = optToken->scope;
                            json["expires_at"] =
                                (Json::Int64)optToken->expiresAt;
                            json["revoked"] = optToken->revoked;

                            auto now = std::chrono::duration_cast<
                                           std::chrono::seconds>(
                                           std::chrono::system_clock::now()
                                               .time_since_epoch())
                                           .count();
                            long ttl = optToken->expiresAt - now;
                            if (ttl > 0)
                            {
                                std::string key = "oauth2:token:" + token;
                                redisClient_->execCommandAsync(
                                    [](const drogon::nosql::RedisResult &) {},
                                    [](const std::exception &) {},
                                    "SET %s %s EX %d",
                                    key.c_str(),
                                    json.toStyledString().c_str(),
                                    ttl);
                            }
                        }
                        (*sharedCb)(optToken);
                    });
            }
            else if (r.type() == drogon::nosql::RedisResultType::kString)
            {
                // Cache Hit
                std::string jsonStr = r.asString();
                Json::Value json;
                Json::Reader reader;
                if (reader.parse(jsonStr, json))
                {
                    OAuth2AccessToken t;
                    t.token = json["token"].asString();
                    t.clientId = json["client_id"].asString();
                    t.userId = json["user_id"].asString();
                    t.scope = json["scope"].asString();
                    t.expiresAt = json["expires_at"].asInt64();
                    t.revoked = json["revoked"].asBool();
                    (*sharedCb)(t);
                }
                else
                {
                    // Parse Error -> Fallback to DB
                    impl_->getAccessToken(token, [sharedCb](auto val) {
                        (*sharedCb)(val);
                    });
                }
            }
            else
            {
                impl_->getAccessToken(token, [sharedCb](auto val) {
                    (*sharedCb)(val);
                });
            }
        },
        [this, token, sharedCb](const std::exception &e) {
            LOG_ERROR << "Redis Read Error: " << e.what();
            impl_->getAccessToken(token,
                                  [sharedCb](auto val) { (*sharedCb)(val); });
        },
        "GET %s",
        key.c_str());
}

void CachedOAuth2Storage::saveRefreshToken(const OAuth2RefreshToken &token,
                                           VoidCallback &&cb)
{
    impl_->saveRefreshToken(token, std::move(cb));
}

void CachedOAuth2Storage::getRefreshToken(const std::string &token,
                                          RefreshTokenCallback &&cb)
{
    impl_->getRefreshToken(token, std::move(cb));
}

void CachedOAuth2Storage::deleteExpiredData()
{
    impl_->deleteExpiredData();
}

void CachedOAuth2Storage::getUserRoles(const std::string &userId,
                                       StringListCallback &&cb)
{
    // Caching Strategy: We could cache user roles in Redis key
    // "oauth2:roles:{userId}" For now, pass through to underlying storage
    // (Postgres) which is efficient enough for login
    impl_->getUserRoles(userId, std::move(cb));
}

}  // namespace oauth2
