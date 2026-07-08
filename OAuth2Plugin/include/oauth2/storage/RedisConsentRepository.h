#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of RedisOAuth2Storage
// into per-aggregate implementation files, mirroring the Task 9 Postgres
// split. This one implements IConsentRepository (REPOSITORY_MAPPING.md
// #26-28). It is ADDITIVE: RedisOAuth2Storage / IOAuth2Storage are untouched
// and remain the production path used by OAuth2Plugin.cc today.
#include <oauth2/storage/IConsentRepository.h>
#include <oauth2/storage/RedisRepositoryBase.h>

#include <memory>

namespace oauth2
{

/**
 * @brief Redis implementation of IConsentRepository.
 *
 * F4 (design.md §7.2): the interface takes UserRef instead of a bare
 * int32_t internalUserId. Per UserRef.h's documented contract, ONLY
 * storage-layer implementations like this one are allowed to unwrap
 * `user.internalUserId` to build the actual Redis key -- that unwrap happens
 * here (once per method), then the key/command logic itself is
 * byte-for-byte the same as the original
 * RedisOAuth2Storage::hasUserConsent/saveUserConsent/revokeUserConsent.
 */
class RedisConsentRepository : public IConsentRepository,
                               public RedisRepositoryBase,
                               public std::enable_shared_from_this<RedisConsentRepository>
{
  public:
    explicit RedisConsentRepository(const std::string &redisClientName = "default")
        : RedisRepositoryBase(redisClientName)
    {
    }

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
