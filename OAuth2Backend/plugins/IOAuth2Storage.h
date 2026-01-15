#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace oauth2 {

/**
 * @brief OAuth2 Client data structure
 */
struct OAuth2Client {
    std::string clientId;
    std::string clientSecretHash;
    std::string salt;
    std::vector<std::string> redirectUris;
    std::vector<std::string> allowedScopes;
};

/**
 * @brief Authorization Code data structure
 */
struct OAuth2AuthCode {
    std::string code;
    std::string clientId;
    std::string userId;
    std::string scope;
    std::string redirectUri;
    std::string codeChallenge;       // PKCE support
    std::string codeChallengeMethod; // "plain" or "S256"
    int64_t expiresAt;               // Unix timestamp (seconds)
    bool used = false;
};

/**
 * @brief Access Token data structure
 */
struct OAuth2AccessToken {
    std::string token;
    std::string clientId;
    std::string userId;
    std::string scope;
    int64_t expiresAt;  // Unix timestamp (seconds)
    bool revoked = false;
};

/**
 * @brief Refresh Token data structure
 */
struct OAuth2RefreshToken {
    std::string token;
    std::string accessToken;
    std::string clientId;
    std::string userId;
    std::string scope;
    int64_t expiresAt;
    bool revoked = false;
};

/**
 * @brief Abstract storage interface for OAuth2 data
 * 
 * Implementations can use different backends:
 * - MemoryOAuth2Storage: In-memory storage for development/testing
 * - PostgresOAuth2Storage: PostgreSQL for production
 * - RedisOAuth2Storage: Redis for high-performance caching
 */
class IOAuth2Storage {
public:
    virtual ~IOAuth2Storage() = default;

    // ========== Client Operations ==========
    
    /**
     * @brief Get client by ID
     * @return Client data or nullopt if not found
     */
    virtual std::optional<OAuth2Client> getClient(const std::string& clientId) = 0;

    /**
     * @brief Validate client credentials
     * @param clientId Client identifier
     * @param clientSecret Plain-text secret to verify against stored hash
     * @return true if valid
     */
    virtual bool validateClient(const std::string& clientId, 
                                const std::string& clientSecret = "") = 0;

    // ========== Authorization Code Operations ==========
    
    /**
     * @brief Save a new authorization code
     */
    virtual void saveAuthCode(const OAuth2AuthCode& code) = 0;

    /**
     * @brief Get authorization code by code value
     * @return AuthCode data or nullopt if not found/expired
     */
    virtual std::optional<OAuth2AuthCode> getAuthCode(const std::string& code) = 0;

    /**
     * @brief Mark an authorization code as used (single-use enforcement)
     */
    virtual void markAuthCodeUsed(const std::string& code) = 0;

    /**
     * @brief Delete expired authorization codes
     */
    virtual void cleanupExpiredAuthCodes() = 0;

    // ========== Access Token Operations ==========
    
    /**
     * @brief Save a new access token
     */
    virtual void saveAccessToken(const OAuth2AccessToken& token) = 0;

    /**
     * @brief Get access token by token value
     * @return Token data or nullopt if not found/expired/revoked
     */
    virtual std::optional<OAuth2AccessToken> getAccessToken(const std::string& token) = 0;

    /**
     * @brief Revoke an access token
     */
    virtual void revokeAccessToken(const std::string& token) = 0;

    /**
     * @brief Revoke all tokens for a user
     */
    virtual void revokeAllUserTokens(const std::string& userId) = 0;

    // ========== Refresh Token Operations ==========
    
    /**
     * @brief Save a new refresh token
     */
    virtual void saveRefreshToken(const OAuth2RefreshToken& token) = 0;

    /**
     * @brief Get refresh token by token value
     */
    virtual std::optional<OAuth2RefreshToken> getRefreshToken(const std::string& token) = 0;

    /**
     * @brief Revoke a refresh token
     */
    virtual void revokeRefreshToken(const std::string& token) = 0;
};

} // namespace oauth2
