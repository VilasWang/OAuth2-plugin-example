#include "OAuth2Plugin.h"
#include "MemoryOAuth2Storage.h"
#include "PostgresOAuth2Storage.h"
#include "RedisOAuth2Storage.h"
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <chrono>

using namespace drogon;

void OAuth2Plugin::initAndStart(const Json::Value &config)
{
    LOG_INFO << "OAuth2Plugin loading...";
    initStorage(config);
    LOG_INFO << "OAuth2Plugin initialized with storage type: " << storageType_;
}

void OAuth2Plugin::initStorage(const Json::Value &config)
{
    storageType_ = config.get("storage_type", "memory").asString();
    
    if (storageType_ == "postgres") {
        auto s = std::make_unique<oauth2::PostgresOAuth2Storage>();
        s->initFromConfig(config["postgres"]); // Always call to get defaults if missing
        storage_ = std::move(s);
        LOG_INFO << "Using PostgreSQL storage backend";
    }
    else if (storageType_ == "redis") {
        storage_ = oauth2::createRedisStorage(config["redis"]);
        LOG_INFO << "Using Redis storage backend";
    }
    else {
        auto s = std::make_unique<oauth2::MemoryOAuth2Storage>();
        if (config.isMember("clients")) s->initFromConfig(config["clients"]);
        storage_ = std::move(s);
        LOG_INFO << "Using in-memory storage backend";
    }
}

void OAuth2Plugin::shutdown()
{
    LOG_INFO << "OAuth2Plugin shutdown";
    storage_.reset();
}

void OAuth2Plugin::validateClient(const std::string &clientId, 
                                  const std::string &clientSecret, 
                                  std::function<void(bool)>&& callback)
{
    if (!storage_) { callback(false); return; }
    storage_->validateClient(clientId, clientSecret, std::move(callback));
}

void OAuth2Plugin::validateRedirectUri(const std::string &clientId, 
                                       const std::string &redirectUri,
                                       std::function<void(bool)>&& callback)
{
    if (!storage_) { callback(false); return; }
    
    // We need to getClient first, then check URIs
    storage_->getClient(clientId, 
        [callback = std::move(callback), redirectUri](std::optional<oauth2::OAuth2Client> client) {
            if (!client) {
                callback(false);
                return;
            }
            for (const auto& uri : client->redirectUris) {
                if (uri == redirectUri) {
                    callback(true);
                    return;
                }
            }
            callback(false);
        }
    );
}

void OAuth2Plugin::generateAuthorizationCode(const std::string &clientId, 
                                             const std::string &userId, 
                                             const std::string &scope,
                                             std::function<void(std::string)>&& callback)
{
    if (!storage_) { callback(""); return; }

    auto code = utils::getUuid();
    oauth2::OAuth2AuthCode authCode;
    authCode.code = code;
    authCode.clientId = clientId;
    authCode.userId = userId;
    authCode.scope = scope;
    
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    authCode.expiresAt = now + 600; // 10 mins

    storage_->saveAuthCode(authCode, [callback = std::move(callback), code]() {
        callback(code);
    });
}

void OAuth2Plugin::exchangeCodeForToken(const std::string &code, 
                                        const std::string &clientId,
                                        std::function<void(std::string)>&& callback)
{
    if (!storage_) { callback(""); return; }

    storage_->getAuthCode(code, 
        [this, callback = std::move(callback), clientId, code](std::optional<oauth2::OAuth2AuthCode> authCode) {
            if (!authCode) {
                LOG_WARN << "Invalid code: " << code;
                callback("");
                return;
            }
            if (authCode->clientId != clientId) {
                callback("");
                return;
            }
            if (authCode->used) {
                LOG_WARN << "Code already used (Replay Attack Attempt): " << code;
                callback("");
                return;
            }
            
            // Mark used
            storage_->markAuthCodeUsed(code, [this, callback, authCode]() {
                 // Generate Token
                 auto tokenStr = utils::getUuid();
                 oauth2::OAuth2AccessToken token;
                 token.token = tokenStr;
                 token.clientId = authCode->clientId;
                 token.userId = authCode->userId;
                 token.scope = authCode->scope;
                 
                 auto now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                 ).count();
                 token.expiresAt = now + 3600;

                 storage_->saveAccessToken(token, [callback, tokenStr, token]() {
                     LOG_INFO << "[AUDIT] Action=IssueAccessToken User=" << token.userId 
                              << " Client=" << token.clientId << " Success=True";
                     callback(tokenStr);
                 });
            });
        }
    );
}

void OAuth2Plugin::validateAccessToken(const std::string &token,
                                       std::function<void(std::shared_ptr<AccessToken>)>&& callback)
{
    if (!storage_) { callback(nullptr); return; }
    
    storage_->getAccessToken(token, 
        [callback](std::optional<oauth2::OAuth2AccessToken> t) {
            if (!t) {
                callback(nullptr);
            } else {
                callback(std::make_shared<oauth2::OAuth2AccessToken>(*t));
            }
        }
    );
}
