#pragma once

// Task 19 (fulla-sdk-refactor, design.md §5.1/§6): real (non-placeholder)
// implementation. AuthService is framework-independent business logic --
// its only dependencies are the identity-owned IUserRepository
// (persistence) plus one fulla::common::ports seam (ICryptoProvider,
// for password hashing/verification), never Drogon or OAuth2Plugin
// directly (design.md §4.1 rule 1). This mirrors
// OAuth2Server/AuthService.cc's validateUser/registerUser/getUserInfo
// semantics (login-identifier routing, progressive lockout backoff,
// hash-on-verify upgrade of legacy hashes, default-role assignment on
// registration) but ported to the injected-port/repository style instead
// of calling drogon::app().getDbClient() or fulla::common::utils::PasswordHasher
// directly. Time-based lockout arithmetic uses IClock instead of
// std::chrono::system_clock::now() so it stays testable with a fake clock
// (design.md §5.6).

#include <fulla/common/ports/ICryptoProvider.h>
#include <fulla/common/ports/IClock.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <json/json.h>

namespace fulla::identity
{

class IUserRepository;

/**
 * @brief Result of a successful user authentication.
 */
struct AuthResult
{
    int32_t internalId = 0;      // Internal auto-increment ID (for DB operations)
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
    // #103: gate for the legacy unsalted-SHA256 verification branch
    // (auth.allow_legacy_hash). Assembly semantics: the window is CLOSED
    // by default — IdentityAssembly treats a missing config key as false
    // (this field initializer stays true only so the api-diff SDK baseline
    // line is untouched). Operators reopen the window explicitly during a
    // migration; see docs/operate/configuration-guide.md.
    void setAllowLegacyHash(bool allow) { allowLegacyHash_ = allow; }

    // #103: optional observer invoked whenever a login is rejected because
    // the stored hash is legacy-format AND the window is closed. Pure
    // observability hook (the domain layer stays logging-free): assembly
    // wires it to a WARN + audit action carrying the internal user id so
    // operators can find and migrate affected accounts. The client still
    // only sees the generic AUTH_INVALID_CREDENTIALS (no oracle).
    void setLegacyHashRejectionNotifier(std::function<void(int32_t internalUserId)> notifier)
    {
        legacyHashRejectionNotifier_ = std::move(notifier);
    }

  public:
    /**
     * @brief Construct auth service with dependencies.
     * @param userRepo Persistence for user records (required).
     * @param crypto Password hashing/verification port (required).
     * @param clock Time source for lockout-window arithmetic (required).
     */
    AuthService(
      std::shared_ptr<IUserRepository> userRepo,
      std::shared_ptr<fulla::common::ports::ICryptoProvider> crypto,
      std::shared_ptr<fulla::common::ports::IClock> clock
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
    void getUserInfo(int32_t userId, std::function<void(std::optional<Json::Value>)> &&callback);

  private:
    std::shared_ptr<IUserRepository> userRepo_;
    std::shared_ptr<fulla::common::ports::ICryptoProvider> crypto_;
    std::shared_ptr<fulla::common::ports::IClock> clock_;
  private:
    bool allowLegacyHash_ = true;
    std::function<void(int32_t)> legacyHashRejectionNotifier_;
};

}  // namespace fulla::identity
