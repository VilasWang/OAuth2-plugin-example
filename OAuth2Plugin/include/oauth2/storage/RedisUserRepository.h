#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of RedisOAuth2Storage
// into per-aggregate implementation files, mirroring the Task 9 Postgres
// split. This one implements IUserRepository (REPOSITORY_MAPPING.md #17-18).
// It is ADDITIVE: RedisOAuth2Storage / IOAuth2Storage are untouched and
// remain the production path used by OAuth2Plugin.cc today.
//
// Physical location / namespace note (mirrors IUserRepository.h's own header
// comment): this repository is conceptually identity-domain, but Task 10
// does not move it to libs/identity (that is M2.5 / Task 19). It stays
// under OAuth2Plugin/{include,src}/oauth2/storage/ in namespace `oauth2`,
// exactly like PostgresUserRepository from Task 9.
#include <oauth2/storage/IUserRepository.h>
#include <oauth2/storage/RedisRepositoryBase.h>

#include <memory>

namespace oauth2
{

/**
 * @brief Redis implementation of IUserRepository.
 *
 * Verbatim preservation of RedisOAuth2Storage's current-state behavior: both
 * overloads return std::nullopt unconditionally ("Redis storage doesn't
 * maintain user details"). This is NOT a new decision made by this split --
 * it is the existing production behavior, carried over as-is.
 */
class RedisUserRepository : public IUserRepository,
                            public RedisRepositoryBase,
                            public std::enable_shared_from_this<RedisUserRepository>
{
  public:
    explicit RedisUserRepository(const std::string &redisClientName = "default")
        : RedisRepositoryBase(redisClientName)
    {
    }

    void getUserInfo(const std::string &userId, OptionalJsonCallback &&cb) override;
    void getUserInfo(int32_t internalUserId, OptionalJsonCallback &&cb) override;
};

}  // namespace oauth2
