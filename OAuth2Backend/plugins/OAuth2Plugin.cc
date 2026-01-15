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
    
    // Initialize storage backend based on configuration
    initStorage(config);
    
    LOG_INFO << "OAuth2Plugin initialized with storage type: " << storageType_;
}

void OAuth2Plugin::initStorage(const Json::Value &config)
{
    storageType_ = config.get("storage_type", "memory").asString();
    
    if (storageType_ == "postgres")
    {
        auto postgresStorage = std::make_unique<oauth2::PostgresOAuth2Storage>();
        if (config.isMember("postgres")) {
            postgresStorage->initFromConfig(config["postgres"]);
        }
        storage_ = std::move(postgresStorage);
        LOG_INFO << "Using PostgreSQL storage backend";
    }
    else if (storageType_ == "redis")
    {
        Json::Value redisConfig;
        if (config.isMember("redis")) {
            redisConfig = config["redis"];
        }
        storage_ = oauth2::createRedisStorage(redisConfig);
        LOG_INFO << "Using Redis storage backend";
    }
    else
    {
        // Default to memory storage
        auto memoryStorage = std::make_unique<oauth2::MemoryOAuth2Storage>();
        if (config.isMember("clients")) {
            memoryStorage->initFromConfig(config["clients"]);
        }
        storage_ = std::move(memoryStorage);
        LOG_INFO << "Using in-memory storage backend";
    }
}

void OAuth2Plugin::shutdown()
{
    LOG_INFO << "OAuth2Plugin shutdown";
    storage_.reset();
}

bool OAuth2Plugin::validateClient(const std::string &clientId, const std::string &clientSecret)
{
    if (!storage_) {
        LOG_ERROR << "Storage not initialized";
        return false;
    }
    return storage_->validateClient(clientId, clientSecret);
}

bool OAuth2Plugin::validateRedirectUri(const std::string &clientId, const std::string &redirectUri)
{
    if (!storage_) {
        return false;
    }
    
    auto client = storage_->getClient(clientId);
    if (!client) {
        return false;
    }
    
    // Check if redirect URI matches any registered URI
    for (const auto& uri : client->redirectUris) {
        if (uri == redirectUri) {
            return true;
        }
    }
    
    return false;
}

std::string OAuth2Plugin::generateAuthorizationCode(const std::string &clientId, 
                                                     const std::string &userId, 
                                                     const std::string &scope)
{
    if (!storage_) {
        LOG_ERROR << "Storage not initialized";
        return "";
    }
    
    auto code = utils::getUuid();
    
    oauth2::OAuth2AuthCode authCode;
    authCode.code = code;
    authCode.clientId = clientId;
    authCode.userId = userId;
    authCode.scope = scope;
    authCode.redirectUri = ""; // Will be validated separately
    authCode.codeChallenge = "";
    authCode.codeChallengeMethod = "";
    
    // Expire in 10 minutes
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    authCode.expiresAt = now + 600; // 10 minutes
    authCode.used = false;
    
    storage_->saveAuthCode(authCode);
    
    return code;
}

std::string OAuth2Plugin::exchangeCodeForToken(const std::string &code, const std::string &clientId)
{
    if (!storage_) {
        LOG_ERROR << "Storage not initialized";
        return "";
    }
    
    auto authCode = storage_->getAuthCode(code);
    if (!authCode) {
        LOG_WARN << "Invalid or expired authorization code: " << code;
        return "";
    }
    
    if (authCode->clientId != clientId) {
        LOG_WARN << "Client ID mismatch for code";
        return "";
    }
    
    // Mark code as used (single-use enforcement)
    storage_->markAuthCodeUsed(code);
    
    // Generate access token
    auto tokenStr = utils::getUuid();
    
    oauth2::OAuth2AccessToken accessToken;
    accessToken.token = tokenStr;
    accessToken.clientId = clientId;
    accessToken.userId = authCode->userId;
    accessToken.scope = authCode->scope;
    
    // Expire in 1 hour
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    accessToken.expiresAt = now + 3600; // 1 hour
    accessToken.revoked = false;
    
    storage_->saveAccessToken(accessToken);
    
    return tokenStr;
}

std::shared_ptr<OAuth2Plugin::AccessToken> OAuth2Plugin::validateAccessToken(const std::string &token)
{
    if (!storage_) {
        return nullptr;
    }
    
    auto storedToken = storage_->getAccessToken(token);
    if (!storedToken) {
        return nullptr;
    }
    
    // Convert to legacy AccessToken structure for API compatibility
    auto result = std::make_shared<AccessToken>();
    result->token = storedToken->token;
    result->clientId = storedToken->clientId;
    result->userId = storedToken->userId;
    result->scope = storedToken->scope;
    
    // Convert expiresAt to timestamp and remaining seconds
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    result->timestamp = now;
    result->expires = storedToken->expiresAt - now; // Remaining seconds
    
    return result;
}

std::shared_ptr<OAuth2Plugin::Client> OAuth2Plugin::getClient(const std::string &clientId)
{
    if (!storage_) {
        return nullptr;
    }
    
    auto storedClient = storage_->getClient(clientId);
    if (!storedClient) {
        return nullptr;
    }
    
    // Convert to legacy Client structure for API compatibility
    auto result = std::make_shared<Client>();
    result->clientId = storedClient->clientId;
    result->clientSecret = storedClient->clientSecretHash;
    result->redirectUri = storedClient->redirectUris.empty() ? "" : storedClient->redirectUris[0];
    
    return result;
}
