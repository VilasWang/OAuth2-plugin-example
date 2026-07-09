#pragma once

// M3 Task 20 slice 7 (authforge-sdk-refactor): relocated from
// OAuth2Server/AuthService.h into authforge::drogon::services --
// SessionController (this slice's migration) depends on this class, so
// it must move together to avoid a circular dependency (libs/drogon <->
// OAuth2Server) that would otherwise result from leaving it behind.

#include <drogon/drogon.h>
#include <functional>
#include <string>
#include <optional>

namespace authforge::drogon::services
{

/**
 * @brief Result of a successful user authentication
 */
struct AuthResult
{
    int internalId;              // Internal auto-increment ID (for DB operations)
    std::string publicSub;       // Public UUID subject (for OAuth2 tokens, never expose internalId)
    bool emailVerified = false;  // Whether email is verified
    bool mfaEnabled = false;     // Whether MFA (TOTP) is enabled
};

class AuthService
{
  public:
    /**
     * @brief Async validate user credentials
     * @param identifier Login identifier — email or username (含 @ 按 email 查，否则按 username 查)
     * @param callback Returns AuthResult on success, nullopt on failure
     */
    static void validateUser(
      const std::string &identifier,
      const std::string &password,
      std::function<void(std::optional<AuthResult>)> &&callback
    );

    /**
     * @brief Async register a new user
     * @param callback Invoked with an empty string on success, or a structured
     *                 Error_Code (registered in ErrorCatalog) on failure. The
     *                 caller forwards the value verbatim to ErrorResponder.
     */
    static void registerUser(
      const std::string &username,
      const std::string &password,
      const std::string &email,
      std::function<void(const std::string &errorCode)> &&callback
    );

    /**
     * @brief Fetch user info and roles from database
     * @param userId Internal user ID
     * @param callback Returns user info JSON or nullopt if not found
     */
    static void getUserInfo(
      int userId,
      std::function<void(std::optional<Json::Value> userInfo)> &&callback
    );
};

}  // namespace authforge::drogon::services
