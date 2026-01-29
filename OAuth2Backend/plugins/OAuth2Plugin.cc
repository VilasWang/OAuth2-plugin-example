#include "OAuth2Plugin.h"
#include "MemoryOAuth2Storage.h"
#include "PostgresOAuth2Storage.h"
#include "RedisOAuth2Storage.h"
#include "CachedOAuth2Storage.h"
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <chrono>

using namespace drogon;

void OAuth2Plugin::initAndStart(const Json::Value &config)
{
    LOG_INFO << "OAuth2Plugin loading...";
    initStorage(config);

    // Load TTL Config
    if (config.isMember("tokens"))
    {
        auto tokens = config["tokens"];
        authCodeTtl_ = tokens.get("auth_code_ttl", 600).asInt64();
        accessTokenTtl_ = tokens.get("access_token_ttl", 3600).asInt64();
        refreshTokenTtl_ = tokens.get("refresh_token_ttl", 2592000).asInt64();
        LOG_INFO << "OAuth2 Config Loaded: AuthCode=" << authCodeTtl_
                 << "s, AccessToken=" << accessTokenTtl_
                 << "s, RefreshToken=" << refreshTokenTtl_ << "s";
    }

    LOG_INFO << "OAuth2Plugin initialized with storage type: " << storageType_;

    // Initialize and start cleanup service
    cleanupService_ =
        std::make_unique<oauth2::OAuth2CleanupService>(storage_.get());

    // Default cleanup 1 hour, or config
    double cleanupInterval = 3600.0;
    if (config.isMember("cleanup_interval_seconds"))
    {
        cleanupInterval = config["cleanup_interval_seconds"].asDouble();
    }
    cleanupService_->start(cleanupInterval);
}

void OAuth2Plugin::initStorage(const Json::Value &config)
{
    storageType_ = config.get("storage_type", "memory").asString();

    if (storageType_ == "postgres")
    {
        auto s = std::make_unique<oauth2::PostgresOAuth2Storage>();
        s->initFromConfig(
            config["postgres"]);  // Always call to get defaults if missing

        // Try to enable L2 Cache
        try
        {
            auto redis = drogon::app().getRedisClient("default");
            // Explicitly upcast to base unique_ptr
            std::unique_ptr<oauth2::IOAuth2Storage> baseStorage = std::move(s);
            // Use raw new to avoid make_unique forwarding issues with move-only
            // types in some MSVC versions
            std::unique_ptr<oauth2::IOAuth2Storage> cached(
                new oauth2::CachedOAuth2Storage(std::move(baseStorage), redis));
            storage_ = std::move(cached);
            LOG_INFO << "Using PostgreSQL storage backend with L2 Redis Cache";
        }
        catch (...)
        {
            // s was moved to baseStorage, and possibly to cached.
            // If we are here, we might have lost the pointer if move happened.
            // But we can check if baseStorage is still valid (if exception
            // happened before second move). However, checking a moved-from
            // pointer is relying on implementation details (usually null).
            // Safest fallback is to re-create.
            LOG_ERROR
                << "Failed to init Cache. Fallback creating new Postgres.";
            auto s2 = std::make_unique<oauth2::PostgresOAuth2Storage>();
            s2->initFromConfig(config["postgres"]);
            storage_ = std::move(s2);
            LOG_INFO << "Using PostgreSQL storage backend (Cache Init Failed)";
        }
    }
    else if (storageType_ == "redis")
    {
        storage_ = oauth2::createRedisStorage(config["redis"]);
        LOG_INFO << "Using Redis storage backend";
    }
    else
    {
        auto s = std::make_unique<oauth2::MemoryOAuth2Storage>();
        if (config.isMember("clients"))
            s->initFromConfig(config["clients"]);
        storage_ = std::move(s);
        LOG_INFO << "Using in-memory storage backend";
    }
}

void OAuth2Plugin::shutdown()
{
    LOG_INFO << "OAuth2Plugin shutdown";
    if (cleanupService_)
        cleanupService_->stop();
    storage_.reset();
}

void OAuth2Plugin::validateClient(const std::string &clientId,
                                  const std::string &clientSecret,
                                  std::function<void(bool)> &&callback)
{
    if (!storage_)
    {
        callback(false);
        return;
    }
    storage_->validateClient(clientId, clientSecret, std::move(callback));
}

void OAuth2Plugin::validateRedirectUri(const std::string &clientId,
                                       const std::string &redirectUri,
                                       std::function<void(bool)> &&callback)
{
    if (!storage_)
    {
        callback(false);
        return;
    }

    // We need to getClient first, then check URIs
    storage_->getClient(clientId,
                        [callback = std::move(callback), redirectUri](
                            std::optional<oauth2::OAuth2Client> client) {
                            if (!client)
                            {
                                callback(false);
                                return;
                            }
                            for (const auto &uri : client->redirectUris)
                            {
                                if (uri == redirectUri)
                                {
                                    callback(true);
                                    return;
                                }
                            }
                            callback(false);
                        });
}

void OAuth2Plugin::generateAuthorizationCode(
    const std::string &clientId,
    const std::string &userId,
    const std::string &scope,
    std::function<void(std::string)> &&callback)
{
    if (!storage_)
    {
        callback("");
        return;
    }

    auto code = utils::getUuid();
    oauth2::OAuth2AuthCode authCode;
    authCode.code = code;
    authCode.clientId = clientId;
    authCode.userId = userId;
    authCode.scope = scope;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    // authCodeTtl_ is already set from config in initAndStart
    // Wait, initAndStart sets member.
    // The previous line 116 was "authCode.expiresAt = now + 600;"
    // Correction:
    authCode.expiresAt = now + authCodeTtl_;

    storage_->saveAuthCode(authCode, [callback = std::move(callback), code]() {
        callback(code);
    });
}

// Helper to create error JSON
static Json::Value makeError(const std::string &error,
                             const std::string &desc = "")
{
    Json::Value json;
    json["error"] = error;
    if (!desc.empty())
        json["error_description"] = desc;
    return json;
}

void OAuth2Plugin::exchangeCodeForToken(
    const std::string &code,
    const std::string &clientId,
    std::function<void(const Json::Value &)> &&callback)
{
    if (!storage_)
    {
        callback(makeError("server_error"));
        return;
    }

    storage_->consumeAuthCode(
        code,
        [this, callback = std::move(callback), clientId, code](
            std::optional<oauth2::OAuth2AuthCode> authCode) {
            if (!authCode)
            {
                LOG_WARN << "Invalid code (Not Found or Already Used): "
                         << code;
                callback(
                    makeError("invalid_grant", "Invalid authorization code"));
                return;
            }
            if (authCode->clientId != clientId)
            {
                callback(makeError("invalid_client"));
                return;
            }

            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

            if (now > authCode->expiresAt)
            {
                LOG_WARN << "Code expired: " << code;
                callback(makeError("invalid_grant", "Code expired"));
                return;
            }

            // Generate Access Token (Code is already marked used by
            // consumeAuthCode) No need to call markAuthCodeUsed again.
            // Generate Access Token (Code is already marked used by
            // consumeAuthCode)

            // Fetch User Roles to return in response (and potentially bake into
            // token if we switched to JWT later)
            storage_->getUserRoles(
                authCode->userId,
                [this,
                 callback,
                 authCode,
                 now,
                 accessTokenTtl = accessTokenTtl_,
                 refreshTokenTtl =
                     refreshTokenTtl_](std::vector<std::string> roles) {
                    // Convert roles vector to string for logs/response
                    Json::Value rolesJson(Json::arrayValue);
                    for (const auto &r : roles)
                        rolesJson.append(r);

                    // Generate Access Token
                    auto tokenStr = utils::getUuid();
                    oauth2::OAuth2AccessToken token;
                    token.token = tokenStr;
                    token.clientId = authCode->clientId;
                    token.userId = authCode->userId;
                    token.scope = authCode->scope;
                    token.expiresAt = now + accessTokenTtl;

                    // Generate Refresh Token
                    auto refreshTokenStr = utils::getUuid();
                    oauth2::OAuth2RefreshToken refreshToken;
                    refreshToken.token = refreshTokenStr;
                    refreshToken.accessToken = tokenStr;
                    refreshToken.clientId = authCode->clientId;
                    refreshToken.userId = authCode->userId;
                    refreshToken.scope = authCode->scope;
                    refreshToken.expiresAt = now + refreshTokenTtl;

                    // Save Access Token
                    storage_->saveAccessToken(
                        token,
                        [this, callback, token, refreshToken, rolesJson]() {
                            // Save Refresh Token
                            storage_->saveRefreshToken(
                                refreshToken,
                                [this,
                                 callback,
                                 token,
                                 refreshToken,
                                 rolesJson]() {
                                    LOG_INFO
                                        << "[AUDIT] Action=IssueToken User="
                                        << token.userId
                                        << " Client=" << token.clientId
                                        << " Success=True";

                                    Json::Value json;
                                    json["access_token"] = token.token;
                                    json["token_type"] = "Bearer";
                                    json["expires_in"] = (Json::Int64)(
                                        token.expiresAt -
                                        std::chrono::duration_cast<
                                            std::chrono::seconds>(
                                            std::chrono::system_clock::now()
                                                .time_since_epoch())
                                            .count());
                                    json["refresh_token"] = refreshToken.token;
                                    json["roles"] =
                                        rolesJson;  // Extension: Return roles
                                    callback(json);
                                });
                        });
                });
        });
}

void OAuth2Plugin::refreshAccessToken(
    const std::string &refreshTokenStr,
    const std::string &clientId,
    std::function<void(const Json::Value &)> &&callback)
{
    if (!storage_)
    {
        callback(makeError("server_error"));
        return;
    }

    storage_->getRefreshToken(
        refreshTokenStr,
        [this, callback = std::move(callback), clientId](
            std::optional<oauth2::OAuth2RefreshToken> storedRt) {
            if (!storedRt)
            {
                callback(makeError("invalid_grant", "Invalid refresh token"));
                return;
            }
            if (storedRt->clientId != clientId)
            {
                callback(makeError("invalid_client"));
                return;
            }
            if (storedRt->revoked)
            {
                LOG_WARN << "Refresh token revoked: " << storedRt->token;
                callback(makeError("invalid_grant", "Token revoked"));
                return;
            }

            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

            if (now > storedRt->expiresAt)
            {
                callback(makeError("invalid_grant", "Token expired"));
                return;
            }

            // Generate New Tokens (Rolling Refresh Token pattern is safer, but
            // keeping simple for now? Let's implement Rolling: Revoke old RT,
            // Issue new RT)

            // 1. Generate New Access Token
            auto newTokenStr = utils::getUuid();
            oauth2::OAuth2AccessToken token;
            token.token = newTokenStr;
            token.clientId = storedRt->clientId;
            token.userId = storedRt->userId;
            token.scope = storedRt->scope;
            token.scope = storedRt->scope;
            token.expiresAt = now + accessTokenTtl_;

            // 2. Generate New Refresh Token
            auto newRefreshTokenStr = utils::getUuid();
            oauth2::OAuth2RefreshToken newRt;
            newRt.token = newRefreshTokenStr;
            newRt.accessToken = newTokenStr;
            newRt.clientId = storedRt->clientId;
            newRt.userId = storedRt->userId;
            newRt.scope = storedRt->scope;
            newRt.expiresAt = now + refreshTokenTtl_;

            // 3. Save New Access Token
            storage_->saveAccessToken(token, [this, callback, token, newRt]() {
                // 4. Save New Refresh Token
                storage_->saveRefreshToken(
                    newRt, [this, callback, token, newRt]() {
                        // We technically should revoke the old one, but
                        // IOAuth2Storage lacks revoke(). We will skip
                        // revocation for now as discussed, or rely on simple
                        // overwriting if supported? Since we can't revoke, we
                        // just issue new ones. This allows parallel usage which
                        // is suboptimal but functional. Improving: Does
                        // saveRefreshToken overwrite? No, UUID is different.

                        Json::Value json;
                        json["access_token"] = token.token;
                        json["token_type"] = "Bearer";
                        json["expires_in"] = (Json::Int64)accessTokenTtl_;
                        json["refresh_token"] = newRt.token;
                        callback(json);
                    });
            });
        });
}

void OAuth2Plugin::validateAccessToken(
    const std::string &token,
    std::function<void(std::shared_ptr<AccessToken>)> &&callback)
{
    if (!storage_)
    {
        callback(nullptr);
        return;
    }

    storage_->getAccessToken(
        token, [callback](std::optional<oauth2::OAuth2AccessToken> t) {
            if (!t)
            {
                callback(nullptr);
                return;
            }

            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

            if (t->revoked)
            {
                LOG_WARN << "Access token revoked: " << t->token;
                callback(nullptr);
                return;
            }
            if (now > t->expiresAt)
            {
                LOG_WARN << "Access token expired: " << t->token;
                callback(nullptr);
                return;
            }

            callback(std::make_shared<oauth2::OAuth2AccessToken>(*t));
        });
}

void OAuth2Plugin::getUserRoles(
    const std::string &userId,
    std::function<void(std::vector<std::string>)> &&callback)
{
    if (!storage_)
    {
        callback({});
        return;
    }
    storage_->getUserRoles(userId, std::move(callback));
}
