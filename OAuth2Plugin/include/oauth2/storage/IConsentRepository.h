#pragma once

// M1 storage interface split (design.md §7). See IClientRepository.h header
// comment for the general rationale (additive, non-migrating, see
// REPOSITORY_MAPPING.md for the full mapping).
//
// F4 (design.md §7.2 / §5.2/§5.3): this repository's methods must not
// expose the identity subsystem's internal primary key
// (`int32_t internalUserId`) in their signatures, so oauth2's consent
// decisions don't couple to identity storage details. UserRef.h defines the
// transitional placeholder type used here; see that header for the full
// rationale and the migration note pointing at the future
// ISubjectResolver (M2a, libs/common).
#include <oauth2/storage/UserRef.h>

#include <functional>
#include <string>

namespace oauth2
{

/**
 * @brief Repository for user consent grants (per client + scope) -- the
 * "consent" aggregate.
 *
 * Carves out the scope-consent group from the former god interface
 * IOAuth2Storage. See REPOSITORY_MAPPING.md for the full 30-method mapping.
 *
 * SDK consumers MAY implement this interface (design.md §7.1: "可选（不启用
 * consent 则不实现）") -- a deployment that doesn't need per-scope user
 * consent (e.g. all clients pre-trusted) can skip it.
 */
class IConsentRepository
{
  public:
    virtual ~IConsentRepository() = default;

    using BoolCallback = std::function<void(bool)>;
    using VoidCallback = std::function<void()>;

    /**
     * @brief Check if user has granted consent for a specific scope.
     * Original: IOAuth2Storage::hasUserConsent(int32_t internalUserId, ...)
     * -- signature changed per F4 to take UserRef instead of a bare
     * internalUserId.
     */
    virtual void hasUserConsent(
      const UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) = 0;

    /**
     * @brief Save user consent for a specific scope.
     * Original: IOAuth2Storage::saveUserConsent(int32_t internalUserId, ...)
     * -- signature changed per F4 to take UserRef instead of a bare
     * internalUserId.
     */
    virtual void saveUserConsent(
      const UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) = 0;

    /**
     * @brief Revoke user consent for a specific scope.
     * Original: IOAuth2Storage::revokeUserConsent(int32_t internalUserId,
     * ...) -- signature changed per F4 to take UserRef instead of a bare
     * internalUserId.
     */
    virtual void revokeUserConsent(
      const UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      VoidCallback &&cb
    ) = 0;

    // ---------------------------------------------------------------------
    // Decision (see REPOSITORY_MAPPING.md): NO purgeExpired() here.
    //
    // Consent grants have no expiresAt/TTL field in the current model and
    // the original IOAuth2Storage::deleteExpiredData() never purged
    // consent rows in any of the three existing implementations -- once
    // granted, a consent stands until explicitly revoked via
    // revokeUserConsent. Adding a no-op purgeExpired() here would be
    // boilerplate with no real semantics, so it is intentionally omitted.
    // ---------------------------------------------------------------------
};

}  // namespace oauth2
