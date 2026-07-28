#pragma once

// M2.5 identity completion (authforge-sdk-refactor, design.md §5.1/§6):
// repository interface backing MfaService, mirroring the raw SQL
// MfaController.cc (libs/drogon/src/controllers/MfaController.cc) issues
// directly against the `users` table's mfa_* columns
// (mfa_secret/mfa_enabled/mfa_backup_codes/mfa_pending_client_id/
// mfa_pending_redirect_uri). Keeping this as its own narrow interface
// (rather than growing IUserRepository) mirrors IRoleRepository/
// ISubjectMappingRepository's existing precedent: one repository per
// bounded set of columns/queries, not one god interface.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace authforge::identity
{

/**
 * @brief MFA (TOTP) state for a user, as read from the repository.
 */
struct MfaData
{
    std::string secret;  // Base32 TOTP secret; empty if not set up.
    bool enabled = false;
    std::vector<std::string> hashedBackupCodes;
    std::string pendingClientId;  // OAuth2 client_id bound at login time,
                                  // pending MFA verification.
    std::string pendingRedirectUri;
};

/**
 * @brief Repository for MFA-related user state.
 */
class IMfaRepository
{
  public:
    virtual ~IMfaRepository() = default;

    using MfaDataCallback = std::function<void(std::optional<MfaData>)>;
    using BoolCallback = std::function<void(bool)>;

    /// Get the MFA state for a user by internal id, or nullopt if the
    /// user does not exist.
    virtual void getMfaData(int32_t userId, MfaDataCallback &&cb) = 0;

    /// Store a freshly generated TOTP secret (setup step; not yet enabled).
    virtual void setSecret(int32_t userId, const std::string &secret, BoolCallback &&cb) = 0;

    /// Enable MFA and store the hashed backup codes (setup verification step).
    virtual void enable(
      int32_t userId,
      const std::vector<std::string> &hashedBackupCodes,
      BoolCallback &&cb
    ) = 0;

    /// Disable MFA and clear the secret/backup codes.
    virtual void disable(int32_t userId, BoolCallback &&cb) = 0;

    /// Record the OAuth2 client/redirect_uri a login is pending MFA
    /// verification for (so verifyLogin can validate they match).
    virtual void setPendingBinding(
      int32_t userId,
      const std::string &clientId,
      const std::string &redirectUri,
      BoolCallback &&cb
    ) = 0;

    /// Clear the pending client/redirect_uri binding after MFA
    /// verification completes (success or otherwise).
    virtual void clearPendingBinding(int32_t userId, BoolCallback &&cb) = 0;
};

}  // namespace authforge::identity
