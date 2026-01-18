#pragma once

#include "IOAuth2Storage.h"
#include <drogon/drogon.h>

namespace oauth2
{

class RedisOAuth2Storage : public IOAuth2Storage
{
  public:
    RedisOAuth2Storage(const std::string &redisClientName = "default")
        : redisClient_(drogon::app().getRedisClient(redisClientName))
    {
        if (redisClient_)
        {
            redisClient_->setTimeout(3.0);
            LOG_DEBUG << "RedisOAuth2Storage initialized with client: "
                      << redisClientName;
        }
        else
        {
            LOG_ERROR << "RedisOAuth2Storage FAILED to get client: "
                      << redisClientName;
        }
    }

    // Client Operations
    void getClient(const std::string &clientId, ClientCallback &&cb) override;
    void validateClient(const std::string &clientId,
                        const std::string &clientSecret,
                        BoolCallback &&cb) override;

    // Authorization Code Operations
    void saveAuthCode(const OAuth2AuthCode &code, VoidCallback &&cb) override;
    void getAuthCode(const std::string &code, AuthCodeCallback &&cb) override;
    void markAuthCodeUsed(const std::string &code, VoidCallback &&cb) override;
    void consumeAuthCode(const std::string &code,
                         AuthCodeCallback &&cb) override;

    // Access Token Operations
    void saveAccessToken(const OAuth2AccessToken &token,
                         VoidCallback &&cb) override;
    void getAccessToken(const std::string &token,
                        AccessTokenCallback &&cb) override;

    // Refresh Token Operations
    void saveRefreshToken(const OAuth2RefreshToken &token,
                          VoidCallback &&cb) override;
    void getRefreshToken(const std::string &token,
                         RefreshTokenCallback &&cb) override;

  private:
    drogon::nosql::RedisClientPtr redisClient_;
};

// Factory function
std::unique_ptr<IOAuth2Storage> createRedisStorage(const Json::Value &config);

}  // namespace oauth2
