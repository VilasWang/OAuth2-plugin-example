#pragma once

// Task 9 (design.md §7 / REPOSITORY_MAPPING.md): split of PostgresOAuth2Storage
// into per-aggregate implementation files. This one implements
// IConsentRepository (REPOSITORY_MAPPING.md #26-28). It is ADDITIVE:
// PostgresOAuth2Storage / IOAuth2Storage are untouched and remain the
// production path used by OAuth2Plugin.cc today.
#include <oauth2/storage/IConsentRepository.h>
#include <oauth2/storage/PostgresRepositoryBase.h>

#include <memory>

namespace oauth2
{

/**
 * @brief PostgreSQL implementation of IConsentRepository.
 *
 * F4 (design.md §7.2): the interface takes UserRef instead of a bare
 * int32_t internalUserId. Per UserRef.h's documented contract, ONLY
 * storage-layer implementations like this one are allowed to unwrap
 * `user.internalUserId` to build the actual query -- that unwrap happens
 * here (once per method), then the query logic itself is byte-for-byte the
 * same Mapper<Oauth2UserConsents> usage as the original
 * PostgresOAuth2Storage::hasUserConsent/saveUserConsent/revokeUserConsent.
 */
class PostgresConsentRepository : public IConsentRepository,
                                  public PostgresRepositoryBase,
                                  public std::enable_shared_from_this<PostgresConsentRepository>
{
  public:
    PostgresConsentRepository() = default;

    void hasUserConsent(
      const UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) override;

    void saveUserConsent(
      const UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) override;

    void revokeUserConsent(
      const UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      VoidCallback &&cb
    ) override;
};

}  // namespace oauth2
