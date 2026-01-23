#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <functional>

namespace oauth2
{

/**
 * @brief OAuth2 Client data structure
 */
struct OAuth2Client
{
    std::string clientId;
    std::string clientSecretHash;
    std::string salt;
    std::vector<std::string> redirectUris;
    std::vector<std::string> allowedScopes;
};

/**
 * @brief Authorization Code data structure
 */
struct OAuth2AuthCode
{
    std::string code;
    std::string clientId;
    std::string userId;
    std::string scope;
    std::string redirectUri;
    std::string codeChallenge;        // PKCE support
    std::string codeChallengeMethod;  // "plain" or "S256"
    int64_t expiresAt;                // Unix timestamp (seconds)
    bool used = false;
};

/**
 * @brief Access Token data structure
 */
struct OAuth2AccessToken
{
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
struct OAuth2RefreshToken
{
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
 * Implementations use ASYNCHRONOUS CALLBACKS.
 * Callbacks are invoked when the operation completes.
 */
class IOAuth2Storage
{
  public:
    virtual ~IOAuth2Storage() = default;

    // Callback types
    using ClientCallback = std::function<void(std::optional<OAuth2Client>)>;
    using AuthCodeCallback = std::function<void(std::optional<OAuth2AuthCode>)>;
    using AccessTokenCallback =
        std::function<void(std::optional<OAuth2AccessToken>)>;
    using RefreshTokenCallback =
        std::function<void(std::optional<OAuth2RefreshToken>)>;
    using VoidCallback = std::function<void()>;
    using BoolCallback = std::function<void(bool)>;

    // ========== Client Operations ==========

    /**
     * @brief Get client by ID
     */
    virtual void getClient(const std::string &clientId,
                           ClientCallback &&cb) = 0;

    /**
     * @brief Validate client credentials
     */
    virtual void validateClient(const std::string &clientId,
                                const std::string &clientSecret,
                                BoolCallback &&cb) = 0;

    // ========== Authorization Code Operations ==========

    /**
     * @brief Save a new authorization code
     */
    virtual void saveAuthCode(const OAuth2AuthCode &code,
                              VoidCallback &&cb) = 0;

    /**
     * @brief Get authorization code by code value
     */
    virtual void getAuthCode(const std::string &code,
                             AuthCodeCallback &&cb) = 0;

    /**
     * @brief Mark an authorization code as used (single-use enforcement)
     */
    virtual void markAuthCodeUsed(const std::string &code,
                                  VoidCallback &&cb) = 0;

    /**
     * @brief Atomic Consume: Get Code, Check if Used, Mark Used, Return Code.
     * If code not found OR already used, callback with std::nullopt.
     */
    virtual void consumeAuthCode(const std::string &code,
                                 AuthCodeCallback &&cb) = 0;

    // ========== Access Token Operations ==========

    /**
     * @brief Save a new access token
     */
    virtual void saveAccessToken(const OAuth2AccessToken &token,
                                 VoidCallback &&cb) = 0;

    /**
     * @brief Get access token by token value
     */
    virtual void getAccessToken(const std::string &token,
                                AccessTokenCallback &&cb) = 0;

    // ========== Refresh Token Operations ==========

    /**
     * @brief Save a new refresh token
     */
    virtual void saveRefreshToken(const OAuth2RefreshToken &token,
                                  VoidCallback &&cb) = 0;

    /**
     * @brief Get refresh token by token value
     */
    virtual void getRefreshToken(const std::string &token,
                                 RefreshTokenCallback &&cb) = 0;

    using StringListCallback = std::function<void(std::vector<std::string>)>;

    // ========== User/Role Operations ==========

    /**
     * @brief Get roles assigned to a user
     * @param userId The ID of the user (as string)
     * @param cb Callback with list of role names
     */
    virtual void getUserRoles(const std::string &userId,
                              StringListCallback &&cb) = 0;

    /**
     * @brief Delete expired data (codes, tokens)
     * Implementations should remove all expired entries.
     */
    virtual void deleteExpiredData() = 0;
};

}  // namespace oauth2
