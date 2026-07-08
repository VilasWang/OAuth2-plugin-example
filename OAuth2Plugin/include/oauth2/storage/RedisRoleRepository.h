#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of RedisOAuth2Storage
// into per-aggregate implementation files, mirroring the Task 9 Postgres
// split. This one implements IRoleRepository (REPOSITORY_MAPPING.md #15-16).
// It is ADDITIVE: RedisOAuth2Storage / IOAuth2Storage are untouched and
// remain the production path used by OAuth2Plugin.cc today.
//
// Physical location / namespace note (mirrors IRoleRepository.h's own header
// comment): this repository is conceptually identity-domain, but Task 10
// does not move it to libs/identity (that is M2.5 / Task 19). It stays
// under OAuth2Plugin/{include,src}/oauth2/storage/ in namespace `oauth2`,
// exactly like PostgresRoleRepository from Task 9.
#include <oauth2/storage/IRoleRepository.h>
#include <oauth2/storage/RedisRepositoryBase.h>

#include <memory>

namespace oauth2
{

/**
 * @brief Redis implementation of IRoleRepository.
 *
 * Verbatim preservation of RedisOAuth2Storage's current-state behavior: both
 * overloads return the hardcoded placeholder role list `{"user"}`
 * unconditionally ("Default role for redis (until we implement role
 * storage in redis)"). This is NOT a new decision made by this split -- it
 * is the existing production behavior, carried over as-is.
 */
class RedisRoleRepository : public IRoleRepository,
                            public RedisRepositoryBase,
                            public std::enable_shared_from_this<RedisRoleRepository>
{
  public:
    explicit RedisRoleRepository(const std::string &redisClientName = "default")
        : RedisRepositoryBase(redisClientName)
    {
    }

    void getUserRoles(const std::string &userId, StringListCallback &&cb) override;
    void getUserRoles(int32_t internalUserId, StringListCallback &&cb) override;
};

}  // namespace oauth2
