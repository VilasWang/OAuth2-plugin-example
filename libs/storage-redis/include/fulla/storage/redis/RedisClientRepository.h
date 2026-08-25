#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of RedisOAuth2Storage
// into per-aggregate implementation files, mirroring the Task 9 Postgres
// split. This one implements IClientRepository (REPOSITORY_MAPPING.md #1-2:
// getClient, validateClient). It is ADDITIVE: RedisOAuth2Storage /
// IOAuth2Storage are untouched and remain the production path used by
// OAuth2Plugin.cc today.
#include <fulla/oauth2/repository/IClientRepository.h>
#include <fulla/storage/redis/RedisRepositoryBase.h>

#include <memory>

namespace fulla::storage::redis
{

// Task 27.5: now implements the NEW Domain-layer interface
// fulla::oauth2::repository::IClientRepository (+ fulla::oauth2::model::* DTOs) instead of
// the legacy oauth2 one. Types are qualified below (not aliased into this namespace) because the
// legacy oauth2::* DTOs still coexist in IOAuth2Storage.h during this transition.
using IClientRepositoryBase = ::fulla::oauth2::repository::IClientRepository;

/**
 * @brief Redis implementation of IClientRepository.
 *
 * Lifetime safety (preserved from RedisOAuth2Storage, "defect 1.8" pattern):
 * inherits std::enable_shared_from_this<RedisClientRepository> so async
 * continuations can capture `auto self = shared_from_this();` and keep this
 * object alive until in-flight Redis callbacks complete. Neither getClient
 * nor validateClient captured `self` in the original RedisOAuth2Storage (only
 * revokeTokenFamily/atomicRevokeRefreshToken/revokeAccessToken did), but this
 * class still inherits the pattern for consistency with the other Redis
 * repository splits and in case a future edit needs it.
 */
class RedisClientRepository : public IClientRepositoryBase,
                              public RedisRepositoryBase,
                              public std::enable_shared_from_this<RedisClientRepository>
{
  public:
    explicit RedisClientRepository(const std::string &redisClientName = "default")
        : RedisRepositoryBase(redisClientName)
    {
    }

    void getClient(const std::string &clientId, ClientCallback &&cb) override;
    void validateClient(
      const std::string &clientId,
      const std::string &clientSecret,
      BoolCallback &&cb
    ) override;
};

}  // namespace fulla::storage::redis
