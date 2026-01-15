#pragma once

#include "IOAuth2Storage.h"
#include <mutex>
#include <unordered_map>
#include <json/json.h>

namespace oauth2 {

/**
 * @brief In-memory implementation of OAuth2 storage
 * 
 * Suitable for development and testing environments.
 * All data is lost on server restart.
 */
class MemoryOAuth2Storage : public IOAuth2Storage {
public:
    /**
     * @brief Initialize with client configuration from JSON
     * @param clientsConfig JSON object with client definitions
     */
    void initFromConfig(const Json::Value& clientsConfig);

    // Client Operations
    std::optional<OAuth2Client> getClient(const std::string& clientId) override;
    bool validateClient(const std::string& clientId, 
                        const std::string& clientSecret = "") override;

    // Authorization Code Operations
    void saveAuthCode(const OAuth2AuthCode& code) override;
    std::optional<OAuth2AuthCode> getAuthCode(const std::string& code) override;
    void markAuthCodeUsed(const std::string& code) override;
    void cleanupExpiredAuthCodes() override;

    // Access Token Operations
    void saveAccessToken(const OAuth2AccessToken& token) override;
    std::optional<OAuth2AccessToken> getAccessToken(const std::string& token) override;
    void revokeAccessToken(const std::string& token) override;
    void revokeAllUserTokens(const std::string& userId) override;

    // Refresh Token Operations
    void saveRefreshToken(const OAuth2RefreshToken& token) override;
    std::optional<OAuth2RefreshToken> getRefreshToken(const std::string& token) override;
    void revokeRefreshToken(const std::string& token) override;

private:
    std::mutex mutex_;
    std::unordered_map<std::string, OAuth2Client> clients_;
    std::unordered_map<std::string, OAuth2AuthCode> authCodes_;
    std::unordered_map<std::string, OAuth2AccessToken> accessTokens_;
    std::unordered_map<std::string, OAuth2RefreshToken> refreshTokens_;
    
    int64_t getCurrentTimestamp() const;
};

} // namespace oauth2
