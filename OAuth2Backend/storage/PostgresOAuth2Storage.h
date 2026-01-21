#pragma once

#include "IOAuth2Storage.h"
#include <drogon/orm/DbClient.h>

namespace oauth2
{

class PostgresOAuth2Storage : public IOAuth2Storage
{
  public:
    PostgresOAuth2Storage() = default;

    /**
     * @brief Initialize from config
     */
    void initFromConfig(const Json::Value &config);

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

    // Cleanup Operations
    void deleteExpiredData() override;

  private:
    drogon::orm::DbClientPtr dbClient_;
    std::string dbClientName_ = "default";
};

}  // namespace oauth2
