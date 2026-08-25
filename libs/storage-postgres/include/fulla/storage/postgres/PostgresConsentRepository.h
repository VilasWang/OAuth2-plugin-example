#pragma once

// Task 9 (design.md §7 / REPOSITORY_MAPPING.md): split of PostgresOAuth2Storage
// into per-aggregate implementation files. This one implements
// IConsentRepository (REPOSITORY_MAPPING.md #26-28). It is ADDITIVE:
// PostgresOAuth2Storage / IOAuth2Storage are untouched and remain the
// production path used by OAuth2Plugin.cc today.
#include <fulla/oauth2/repository/IConsentRepository.h>
#include <fulla/storage/postgres/PostgresRepositoryBase.h>

#include <memory>

namespace fulla::storage::postgres
{

// Task 27.5: now implements the NEW Domain-layer interface
// fulla::oauth2::repository::IConsentRepository (+ fulla::oauth2::model::* DTOs) instead of
// the legacy oauth2 one. Types are qualified below (not aliased into this namespace) because the
// legacy oauth2::* DTOs still coexist in IOAuth2Storage.h during this transition.
using IConsentRepositoryBase = ::fulla::oauth2::repository::IConsentRepository;

/**
 * @brief PostgreSQL implementation of IConsentRepository.
 *
 * F4 (design.md §7.2): the interface takes ::fulla::oauth2::model::UserRef instead of a bare
 * int32_t internalUserId. Per ::fulla::oauth2::model::UserRef.h's documented contract, ONLY
 * storage-layer implementations like this one are allowed to unwrap
 * `user.internalUserId` to build the actual query -- that unwrap happens
 * here (once per method), then the query logic itself is byte-for-byte the
 * same Mapper<Oauth2UserConsents> usage as the original
 * PostgresOAuth2Storage::hasUserConsent/saveUserConsent/revokeUserConsent.
 */
class PostgresConsentRepository : public IConsentRepositoryBase,
                                  public PostgresRepositoryBase,
                                  public std::enable_shared_from_this<PostgresConsentRepository>
{
  public:
    PostgresConsentRepository() = default;

    void hasUserConsent(
      const ::fulla::oauth2::model::UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) override;

    void saveUserConsent(
      const ::fulla::oauth2::model::UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) override;

    void revokeUserConsent(
      const ::fulla::oauth2::model::UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      VoidCallback &&cb
    ) override;
};

}  // namespace fulla::storage::postgres
