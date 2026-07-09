#pragma once

// Task 19 (authforge-sdk-refactor, design.md §5.1/§6): real (non-placeholder)
// implementation. AuthService is framework-independent business logic --
// its only dependencies are the identity-owned IUserRepository
// (persistence) plus one authforge::common::ports seam (ICryptoProvider,
// for password hashing/verification), never Drogon or OAuth2Plugin
// directly (design.md §4.1 rule 1). This mirrors
// OAuth2Server/AuthService.cc's validateUser/registerUser/getUserInfo
// semantics (login-identifier routing, progressive lockout backoff,
// hash-on-verify upgrade of legacy hashes, default-role assignment on
// registration) but ported to the injected-port/repository style instead
// of calling drogon::app().getDbClient() or oauth2::utils::PasswordHasher
// directly. Time-based lockout arithmetic uses IClock instead of
// std::chrono::system_clock::now() so it stays testable with a fake clock
// (design.md §5.6).

#include <authforge/common/ports/ICryptoProvider.h>
#include <authforge/common/ports/IClock.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <json/json.h>

namespace authforge::identity
{

class IUserRepository;

/**
 * @brief Result of a successful user authentication.
 */
struct AuthResult
{
    int64_t internalId = 0;      // Internal auto-increment ID (for DB operations)
    std::string publicSub;       // Public UUID subject (for OAuth2 tokens, never expose internalId)
    bool emailVerified = false;  // Whether email is verified
    bool mfaEnabled = false;     // Whether MFA (TOTP) is enabled
};

/**
 * @brief Core authentication service.
 *
 * Handles user authentication, registration, and userinfo lookup.
 * Framework-independent -- all infrastructure dependencies (persistence,
 * crypto, clock) are injected through the constructor.
 */
class AuthService
{
  public:
    /**
     * @brief Construct auth service with dependencies.
     * @param userRepo Persistence for user records (required).
     * @param crypto Password hashing/verification port (required).
     * @param clock Time source for lockout-window arithmetic (required).
     */
    AuthService(
      std::shared_ptr<IUserRepository> userRepo,
      std::shared_ptr<authforge::common::ports::ICryptoProvider> crypto,
      std::shared_ptr<authforge::common::ports::IClock> clock
    );

    /**
     * @brief Async validate user credentials.
     * @param identifier Login identifier — email or username (contains '@'
     * -> looked up as email, otherwise as username).
     * @param password User password (plain text).
     * @param callback Returns AuthResult on success, nullopt on failure
     * (unknown identifier, wrong password, or account currently locked).
     */
    void validateUser(
      const std::string &identifier,
      const std::string &password,
      std::function<void(std::optional<AuthResult>)> &&callback
    );

    /**
     * @brief Async register a new user.
     * @param username Unique username (may be empty -- email-first model).
     * @param password Plain text password (will be hashed).
     * @param email User email address (normalized before storage/lookup).
     * @param callback Invoked with an empty string on success, or a
     * structured Error_Code (registered in ErrorCatalog) on failure --
     * mirrors OAuth2Server/AuthService.cc's existing contract so callers
     * can forward the value verbatim to ErrorResponder.
     */
    void registerUser(
      const std::string &username,
      const std::string &password,
      const std::string &email,
      std::function<void(const std::string &errorCode)> &&callback
    );

    /**
     * @brief Fetch user info + roles for the userinfo endpoint.
     * @param userId Internal user ID.
     * @param callback Returns user info JSON (sub/name/email/roles) or
     * nullopt if not found.
     */
    void getUserInfo(
      int64_t userId,
      std::function<void(std::optional<Json::Value>)> &&callback
    );

  private:
    std::shared_ptr<IUserRepository> userRepo_;
    std::shared_ptr<authforge::common::ports::ICryptoProvider> crypto_;
    std::shared_ptr<authforge::common::ports::IClock> clock_;
};

}  // namespace authforge::identity
