#pragma once

#include <drogon/plugins/Plugin.h>
#include <oauth2/storage/IOAuth2Storage.h>
#include <oauth2/plugin/OAuth2CleanupService.h>
#include <oauth2/services/IdentityService.h>
#include <oauth2/adapters/StorageRoleProvider.h>
#include <oauth2/utils/JwkManager.h>
#include <authforge/oauth2/protocol/TokenService.h>
#include <authforge/oauth2/protocol/ClientService.h>
#include <string>
#include <memory>
#include <functional>

// M3 Task 24 slice 2 (authforge-sdk-refactor, PROGRESS.md "Task 24 切分
// 方案"): tokenService_/clientService_ now hold the NEW Domain-layer
// classes (authforge::oauth2::protocol::TokenService/ClientService, Task
// 17) instead of the old oauth2::TokenService/ClientService. Every
// forwarding method below (validateClient/generateAuthorizationCode/
// exchangeCodeForToken/etc) keeps its EXACT existing signature -- no
// controller call site changes -- because OAuth2Plugin was already a
// clean forwarding facade (every controller calls plugin->xxx(...), never
// oauth2::TokenService directly). Only OAuth2Plugin.cc's internals change:
// construction goes through a LegacyStorageRepositoryBridge (Task 24
// slice 1) that adapts storage_ (still the old oauth2::IOAuth2Storage) to
// the new split repository interfaces the new services require, and each
// forwarding method's body does the old<->new DTO conversion at the
// boundary. identityService_ (consent/roles/subject-mapping) is
// deliberately UNCHANGED in this slice -- AuthorizationService's
// integration needs ISubjectResolver wiring, which is a separate,
// larger slice (see PROGRESS.md).

class OAuth2Plugin : public drogon::Plugin<OAuth2Plugin>
{
  public:
    using AccessToken = oauth2::OAuth2AccessToken;
    using Client = oauth2::OAuth2Client;

    OAuth2Plugin() = default;
    void initAndStart(const Json::Value &config) override;
    void shutdown() override;

    // ========== Service Accessors ==========
    // M3 Task 24 slice 2: these now return the NEW Domain-layer service
    // types. Grep-confirmed zero call sites use these two accessors
    // (unlike getIdentityService()/getStorage()/getJwkManager(), which
    // are used by controllers) -- safe to change return type without a
    // wider ripple.
    std::shared_ptr<authforge::oauth2::protocol::TokenService> getTokenService() const
    {
        return tokenService_;
    }

    std::shared_ptr<authforge::oauth2::protocol::ClientService> getClientService() const
    {
        return clientService_;
    }

    std::shared_ptr<oauth2::IdentityService> getIdentityService() const
    {
        return identityService_;
    }

    // Defect 1.5 fix (immutable publish): the JwkManager is published as a
    // std::shared_ptr<const JwkManager>. It is built and init()'d exactly once
    // during initAndStart() (before requests are served); thereafter it is
    // read-only, and the const pointer enforces that at the type level for
    // every holder (this plugin, TokenService, the JWKS controller).
    std::shared_ptr<const oauth2::JwkManager> getJwkManager() const
    {
        return jwkManager_;
    }

    // Returns shared ownership of the storage (defect 1.3 / 1.11 fix). Both
    // overloads return std::shared_ptr<IOAuth2Storage> so callers (e.g. the
    // controller async chains) can capture it and keep the storage alive across
    // async hops instead of holding a raw pointer whose lifetime is implicit.
    std::shared_ptr<oauth2::IOAuth2Storage> getStorage() const
    {
        return storage_;
    }

    // ========== Async API with Callbacks ==========

    /**
     * @brief Validate if client exists and secret matches (Async)
     */
    void validateClient(
      const std::string &clientId,
      const std::string &clientSecret,
      std::function<void(bool)> &&callback
    );

    /**
     * @brief Validate redirect URI (Async)
     */
    void validateRedirectUri(
      const std::string &clientId,
      const std::string &redirectUri,
      std::function<void(bool)> &&callback
    );

    /**
     * @brief Generate Authorization Code (Async)
     * @param clientId Client identifier
     * @param subject OAuth2 subject (e.g., "local:alice", "google:sub123")
     * @param scope Requested scopes
     * @param redirectUri Redirect URI
     * @param codeChallenge PKCE code challenge (optional, empty if not
     * provided)
     * @param codeChallengeMethod PKCE code challenge method ("plain", "S256",
     * or empty)
     * @param callback Callback with authorization code or empty string on
     * failure
     */
    void generateAuthorizationCode(
      const std::string &clientId,
      const std::string &subject,
      const std::string &scope,
      const std::string &redirectUri,
      const std::string &codeChallenge,
      const std::string &codeChallengeMethod,
      const std::string &nonce,
      std::function<void(bool, std::string, std::string)> &&callback
    );

    /**
     * @brief Exchange Code for Access Token (Async)
     * Returns JSON with {access_token, refresh_token, expires_in} or {error}
     *
     * @param code Authorization code from authorize endpoint
     * @param clientId Client identifier
     * @param clientSecret Client secret (required for CONFIDENTIAL clients,
     * empty for PUBLIC)
     * @param redirectUri Redirect URI from token request (must match
     * authorization request per OAuth2 RFC 6749 Section 4.1.3)
     * @param callback Callback with token response or error
     */
    void exchangeCodeForToken(
      const std::string &code,
      const std::string &clientId,
      const std::string &clientSecret,
      const std::string &redirectUri,
      const std::string &codeVerifier,  // P0-3: PKCE code verifier
      std::function<void(const Json::Value &)> &&callback
    );

    /**
     * @brief Refresh Access Token (Async)
     * Returns JSON with {access_token, refresh_token, expires_in} or {error}
     */
    void refreshAccessToken(
      const std::string &refreshToken,
      const std::string &clientId,
      std::function<void(const Json::Value &)> &&callback
    );

    /**
     * @brief Validate Access Token (Async)
     */
    void validateAccessToken(
      const std::string &token,
      std::function<void(std::shared_ptr<AccessToken>)> &&callback
    );

    /**
     * @brief Get User Roles (Async)
     */
    void getUserRoles(
      const std::string &userId,
      std::function<void(std::vector<std::string>)> &&callback
    );

    // ========== P0-2: Consent Management Methods ==========

    /**
     * @brief Get internal user ID from subject (Async)
     * @param subject OAuth2 subject (e.g., "local:alice", "google:sub123")
     * @param callback Callback with optional internal user ID
     */
    void getInternalUserId(
      const std::string &subject,
      std::function<void(std::optional<int32_t>)> &&callback
    );

    /**
     * @brief Check if user has consented to a scope for a client (Async)
     * @param internalUserId Internal user ID
     * @param clientId Client identifier
     * @param scope Scope to check consent for
     * @param callback Callback with consent status
     */
    void hasUserConsent(
      int32_t internalUserId,
      const std::string &clientId,
      const std::string &scope,
      std::function<void(bool)> &&callback
    );

    /**
     * @brief Save user consent for a scope (Async)
     * @param internalUserId Internal user ID
     * @param clientId Client identifier
     * @param scope Scope to save consent for
     * @param callback Callback with success status
     */
    void saveUserConsent(
      int32_t internalUserId,
      const std::string &clientId,
      const std::string &scope,
      std::function<void(bool)> &&callback
    );

    // ========== P0-3: PKCE Validation Methods ==========

    /**
     * @brief Validate PKCE code verifier against challenge
     * @param codeVerifier Code verifier from token request
     * @param codeChallenge Code challenge from authorization request
     * @param codeChallengeMethod Challenge method ("plain" or "S256")
     * @return true if verifier is valid, false otherwise
     */
    static bool validatePkceCodeVerifier(
      const std::string &codeVerifier,
      const std::string &codeChallenge,
      const std::string &codeChallengeMethod
    );

    /**
     * @brief Generate SHA-256 hash for PKCE S256 method
     * @param input String to hash
     * @return Base64-url encoded hash
     */
    static std::string generateSha256Hash(const std::string &input);

    // ========== P0-5: Scope Permission Control Methods ==========

    /**
     * @brief Validate requested scopes against client allowlist (Tier 1)
     * @param clientId Client identifier
     * @param requestedScopes Scopes requested by client
     * @param callback Callback with validation result and error message
     */
    void validateClientScopes(
      const std::string &clientId,
      const std::vector<std::string> &requestedScopes,
      std::function<void(bool, std::string)> &&callback
    );

    /**
     * @brief Validate user roles for admin scopes (Tier 2)
     * @param userId User identifier (subject)
     * @param scopes Requested scopes
     * @param callback Callback with validation result and error message
     */
    void validateUserRolesForScopes(
      const std::string &userId,
      const std::vector<std::string> &scopes,
      std::function<void(bool, std::string)> &&callback
    );

    /**
     * @brief Check if scope requires admin role
     * @param scope Scope to check
     * @return true if scope requires admin role, false otherwise
     */
    static bool scopeRequiresAdminRole(const std::string &scope);

    // ========== P1: Token Introspection (RFC 7662) ==========

    /**
     * @brief Introspect token metadata (RFC 7662) (Async)
     * @param token The access token to introspect
     * @param callback Callback with token introspection metadata or nullopt if invalid
     */
    void introspectToken(
      const std::string &token,
      std::function<void(std::optional<oauth2::TokenIntrospection>)> &&callback
    );

    /**
     * @brief Increment introspection count for monitoring (Async)
     * @param token The access token
     * @param callback Callback invoked when update completes
     */
    void incrementIntrospectCount(const std::string &token, std::function<void()> &&callback);

    // ========== P1: Token Revocation (RFC 7009) ==========

    /**
     * @brief Revoke access token with audit trail (RFC 7009) (Async)
     * @param token The access token to revoke
     * @param revokedBy Client ID performing the revocation
     * @param callback Callback invoked when revocation completes
     */
    void revokeAccessToken(
      const std::string &token,
      const std::string &revokedBy,
      std::function<void()> &&callback
    );

    // ========== Storage Access ==========
    std::shared_ptr<oauth2::IOAuth2Storage> getStorage()
    {
        return storage_;
    }

    const std::string &getStorageType() const
    {
        return storageType_;
    }

  private:
    std::shared_ptr<oauth2::IOAuth2Storage> storage_;
    std::shared_ptr<oauth2::OAuth2CleanupService> cleanupService_;
    std::shared_ptr<authforge::oauth2::protocol::TokenService> tokenService_;
    std::shared_ptr<authforge::oauth2::protocol::ClientService> clientService_;
    std::shared_ptr<oauth2::IdentityService> identityService_;
    // M2b Task 17 slice 12: first production instantiation of
    // authforge::common::ports::IRoleProvider (via the Adapter-side
    // StorageRoleProvider, backed by storage_). Not yet consumed by any
    // caller -- IdentityService's role lookups are keyed by subject
    // string today, not the internalUserId this port takes, so wiring it
    // into an actual call site requires ISubjectResolver too (a later
    // slice). Constructed here so the Adapter class has a real production
    // instantiation point once that wiring happens.
    std::shared_ptr<oauth2::adapters::StorageRoleProvider> roleProvider_;
    std::shared_ptr<const oauth2::JwkManager> jwkManager_;

    std::string storageType_;

    // TTL Configuration (Seconds)
    // Note: These are set once during initAndStart() and only read afterwards
    // Thread-safe due to happens-before guarantee (init before requests)
    long long authCodeTtl_{600};
    long long accessTokenTtl_{3600};
    long long refreshTokenTtl_{3600 * 24 * 30};

    void initStorage(const Json::Value &config);

    // ========== Subject Mapping Methods ==========

    /**
     * @brief Ensure subject mapping exists, create if needed
     * @param subject Full subject (e.g., "local:alice")
     * @param username Original username for logging
     * @param internalUserId Internal user ID from users table
     * @param callback Callback invoked when mapping is ensured
     */
    void ensureSubjectMapping(
      const std::string &subject,
      const std::string &username,
      int32_t internalUserId,
      std::function<void()> &&callback
    );

    /**
     * @brief Handle first-time login for new users
     * @param subject Full subject
     * @param provider Provider name
     * @param callback Callback with internal user ID or 0 if failed
     */
    void handleFirstTimeLogin(
      const std::string &subject,
      const std::string &provider,
      std::function<void(int32_t)> &&callback
    );
};
