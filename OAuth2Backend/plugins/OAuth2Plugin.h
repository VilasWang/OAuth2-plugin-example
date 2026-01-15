#pragma once

#include <drogon/plugins/Plugin.h>
#include "IOAuth2Storage.h"
#include <string>
#include <memory>

/**
 * @brief OAuth2 Authorization Server Plugin
 * 
 * Provides OAuth2 authorization code flow with pluggable storage backend.
 * Storage backend is configurable via config.json:
 *   - "memory": In-memory storage (default, for development)
 *   - "postgres": PostgreSQL database (for production)
 */
class OAuth2Plugin : public drogon::Plugin<OAuth2Plugin>
{
public:
    // Legacy structures for API compatibility
    struct Client
    {
        std::string clientId;
        std::string clientSecret;
        std::string redirectUri;
    };

    struct AuthCode
    {
        std::string code;
        std::string clientId;
        std::string userId;
        std::string scope;
        long long timestamp;
    };

    struct AccessToken
    {
        std::string token;
        std::string clientId;
        std::string userId;
        std::string scope;
        long long timestamp;
        long long expires;
    };

    OAuth2Plugin() = default;
    void initAndStart(const Json::Value &config) override;
    void shutdown() override;

    // ========== Client Validation ==========
    
    /**
     * @brief Validate if client exists and secret matches
     */
    bool validateClient(const std::string &clientId, const std::string &clientSecret = "");
    
    /**
     * @brief Validate redirect URI for a client
     */
    bool validateRedirectUri(const std::string &clientId, const std::string &redirectUri);

    // ========== Authorization Code Flow ==========
    
    /**
     * @brief Generate and store Authorization Code
     * @return Generated authorization code string
     */
    std::string generateAuthorizationCode(const std::string &clientId, 
                                          const std::string &userId, 
                                          const std::string &scope);

    /**
     * @brief Exchange Code for Access Token
     * @return Access token string, empty if invalid
     */
    std::string exchangeCodeForToken(const std::string &code, const std::string &clientId);

    // ========== Token Validation ==========
    
    /**
     * @brief Validate Access Token
     * @return Token info or nullptr if invalid
     */
    std::shared_ptr<AccessToken> validateAccessToken(const std::string &token);

    // ========== Client Info ==========
    
    /**
     * @brief Get Client info by ID
     */
    std::shared_ptr<Client> getClient(const std::string &clientId);

    // ========== Storage Access (for advanced use) ==========
    
    /**
     * @brief Get the underlying storage implementation
     */
    oauth2::IOAuth2Storage* getStorage() { return storage_.get(); }

private:
    std::unique_ptr<oauth2::IOAuth2Storage> storage_;
    std::string storageType_;
    
    void initStorage(const Json::Value &config);
};
