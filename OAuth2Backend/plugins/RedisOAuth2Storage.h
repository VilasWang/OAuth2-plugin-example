#pragma once

#include "IOAuth2Storage.h"
#include <drogon/drogon.h>
#include <drogon/nosql/RedisClient.h>

namespace oauth2 {

class RedisOAuth2Storage : public IOAuth2Storage {
public:
    explicit RedisOAuth2Storage(const std::string& redisClientName = "default");
    
    // 初始化配置
    void initFromConfig(const Json::Value& config);

    // Client 操作
    std::optional<OAuth2Client> getClient(const std::string& clientId) override;
    bool validateClient(const std::string& clientId, const std::string& clientSecret) override;

    // Authorization Code 操作
    void saveAuthCode(const OAuth2AuthCode& code) override;
    std::optional<OAuth2AuthCode> getAuthCode(const std::string& code) override;
    void markAuthCodeUsed(const std::string& code) override;
    void cleanupExpiredAuthCodes() override;

    // Access Token 操作
    void saveAccessToken(const OAuth2AccessToken& token) override;
    std::optional<OAuth2AccessToken> getAccessToken(const std::string& token) override;
    void revokeAccessToken(const std::string& token) override;
    void revokeAllUserTokens(const std::string& userId) override;

    // Refresh Token 操作
    void saveRefreshToken(const OAuth2RefreshToken& token) override;
    std::optional<OAuth2RefreshToken> getRefreshToken(const std::string& token) override;
    void revokeRefreshToken(const std::string& token) override;

private:
    std::string redisClientName_;
    drogon::nosql::RedisClientPtr getRedisClient() const;
    
    // Helper to format keys
    std::string getClientKey(const std::string& clientId) const;
    std::string getAuthCodeKey(const std::string& code) const;
    std::string getAccessTokenKey(const std::string& token) const;
    std::string getRefreshTokenKey(const std::string& token) const;
};

std::unique_ptr<IOAuth2Storage> createRedisStorage(const Json::Value& config);

} // namespace oauth2
