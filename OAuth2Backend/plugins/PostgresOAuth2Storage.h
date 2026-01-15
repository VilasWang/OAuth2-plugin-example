#pragma once

#include "IOAuth2Storage.h"
#include <drogon/orm/DbClient.h>
#include <json/json.h>

namespace oauth2 {

/**
 * @brief PostgreSQL implementation of OAuth2 storage
 * 
 * Production-ready storage using Drogon's ORM DbClient.
 * Requires database tables to be created according to the schema.
 */
class PostgresOAuth2Storage : public IOAuth2Storage {
public:
    /**
     * @brief Construct with database client name
     * @param dbClientName Name of the DbClient configured in Drogon (e.g., "default")
     */
    explicit PostgresOAuth2Storage(const std::string& dbClientName = "default");
    
    /**
     * @brief Initialize from JSON configuration
     * @param config Configuration containing database settings
     */
    void initFromConfig(const Json::Value& config);

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
    std::string dbClientName_;
    drogon::orm::DbClientPtr getDbClient() const;
    
    // Helper to verify password against bcrypt hash
    bool verifyPassword(const std::string& password, const std::string& hash) const;
};

} // namespace oauth2
