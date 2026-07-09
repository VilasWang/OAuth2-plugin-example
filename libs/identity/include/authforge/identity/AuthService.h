#pragma once

#include <authforge/common/model/Subject.h>
#include <authforge/common/result/Result.h>
#include <authforge/identity/ISubjectResolver.h>
#include <authforge/identity/IUserInfoProvider.h>
#include <functional>
#include <string>
#include <optional>
#include <memory>
#include <json/json.h>

namespace authforge::identity
{

/**
 * @brief Result of a successful user authentication
 */
struct AuthResult
{
    int64_t internalId;           // Internal auto-increment ID (for DB operations)
    std::string publicSub;        // Public UUID subject (for OAuth2 tokens, never expose internalId)
    bool emailVerified = false;   // Whether email is verified
    bool mfaEnabled = false;      // Whether MFA (TOTP) is enabled
};

/**
 * @brief Core authentication service
 * 
 * Handles user authentication, registration, and basic user operations.
 * Framework-independent - dependencies injected through constructor.
 */
class AuthService
{
public:
    /**
     * @brief Construct auth service with dependencies
     */
    AuthService(
      std::shared_ptr<class IUserRepository> userRepo,
      std::shared_ptr<ISubjectResolver> subjectResolver,
      std::shared_ptr<IUserInfoProvider> userInfoProvider
    );

    /**
     * @brief Async validate user credentials
     * @param identifier Login identifier — email or username (含 @ 按 email 查，否则按 username 查)
     * @param password User password (plain text)
     * @param callback Returns AuthResult on success, nullopt on failure
     */
    void validateUser(
      const std::string &identifier,
      const std::string &password,
      std::function<void(std::optional<AuthResult>)> &&callback
    );

    /**
     * @brief Async register a new user
     * @param username Unique username
     * @param password Plain text password (will be hashed)
     * @param email User email address
     * @param callback Invoked with error code on failure, empty string on success
     */
    void registerUser(
      const std::string &username,
      const std::string &password,
      const std::string &email,
      std::function<void(const std::string &errorCode)> &&callback
    );

    /**
     * @brief Fetch user info (delegates to IUserInfoProvider)
     * @param userId Internal user ID
     * @param scopes Granted scopes (determines which claims to include)
     * @param callback Returns user info JSON or nullopt if not found
     */
    void getUserInfo(
      int64_t userId,
      const std::vector<std::string> &scopes,
      std::function<void(std::optional<Json::Value>)> &&callback
    );

private:
    std::shared_ptr<class IUserRepository> userRepo_;
    std::shared_ptr<ISubjectResolver> subjectResolver_;
    std::shared_ptr<IUserInfoProvider> userInfoProvider_;
};

}  // namespace authforge::identity
