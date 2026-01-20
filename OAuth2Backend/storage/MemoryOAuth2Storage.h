#pragma once

#include "IOAuth2Storage.h"
#include <mutex>
#include <unordered_map>
#include <json/json.h>

namespace oauth2
{

/**
 * @brief In-memory implementation of OAuth2 storage
 *
 * Suitable for development and testing environments.
 * All data is lost on server restart.
 */
class MemoryOAuth2Storage : public IOAuth2Storage
{
  public:
    /**
     * @brief Initialize with client configuration from JSON
     * @param clientsConfig JSON object with client definitions
     */
    void initFromConfig(const Json::Value &clientsConfig);

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
    std::recursive_mutex mutex_;
    std::unordered_map<std::string, OAuth2Client> clients_;
    std::unordered_map<std::string, OAuth2AuthCode> authCodes_;
    std::unordered_map<std::string, OAuth2AccessToken> accessTokens_;
    std::unordered_map<std::string, OAuth2RefreshToken> refreshTokens_;

    int64_t getCurrentTimestamp() const;
};

}  // namespace oauth2
