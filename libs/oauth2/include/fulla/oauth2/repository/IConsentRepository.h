#pragma once

// Task 17 slice 3 (fulla-sdk-refactor, design.md §6/§7.2 评审 F4):
// ports the M1 repository interface oauth2::IConsentRepository
// (OAuth2Plugin/include/oauth2/storage/IConsentRepository.h) into
// fulla::oauth2::repository, using the freshly-ported
// fulla::oauth2::model::UserRef (Task 17 slice 3, model/UserRef.h)
// instead of oauth2::UserRef.

#include <fulla/oauth2/model/UserRef.h>

#include <functional>
#include <string>

namespace fulla::oauth2::repository
{

/**
 * @brief Repository for user consent grants (per client + scope) -- the
 * "consent" aggregate.
 *
 * SDK consumers MAY implement this interface (design.md §7.1: optional --
 * a deployment that doesn't need per-scope user consent can skip it).
 */
class IConsentRepository
{
  public:
    virtual ~IConsentRepository() = default;

    using BoolCallback = std::function<void(bool)>;
    using VoidCallback = std::function<void()>;

    /**
     * @brief Check if user has granted consent for a specific scope.
     * Original: IOAuth2Storage::hasUserConsent(int32_t internalUserId,
     * ...) -- signature changed per F4 to take UserRef instead of a bare
     * internalUserId.
     */
    virtual void hasUserConsent(
      const fulla::oauth2::model::UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) = 0;

    /**
     * @brief Save user consent for a specific scope.
     * Original: IOAuth2Storage::saveUserConsent(int32_t internalUserId,
     * ...) -- signature changed per F4.
     */
    virtual void saveUserConsent(
      const fulla::oauth2::model::UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) = 0;

    /**
     * @brief Revoke user consent for a specific scope.
     * Original: IOAuth2Storage::revokeUserConsent(int32_t internalUserId,
     * ...) -- signature changed per F4.
     */
    virtual void revokeUserConsent(
      const fulla::oauth2::model::UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      VoidCallback &&cb
    ) = 0;

    // ---------------------------------------------------------------------
    // Decision (carried over from REPOSITORY_MAPPING.md): NO
    // purgeExpired() here -- consent grants have no expiresAt/TTL field
    // and the original never purged consent rows.
    // ---------------------------------------------------------------------
};

}  // namespace fulla::oauth2::repository
