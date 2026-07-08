#pragma once

// Task 9 (design.md §7 / REPOSITORY_MAPPING.md): split of PostgresOAuth2Storage
// into per-aggregate implementation files. This one implements
// IUserRepository (REPOSITORY_MAPPING.md #17-18). It is ADDITIVE:
// PostgresOAuth2Storage / IOAuth2Storage are untouched and remain the
// production path used by OAuth2Plugin.cc today.
//
// Physical location / namespace note (mirrors IUserRepository.h's own header
// comment): this repository is conceptually identity-domain, but Task 9 does
// not move it to libs/identity (that is M2.5 / Task 19). It stays under
// OAuth2Plugin/{include,src}/oauth2/storage/ in namespace `oauth2`.
#include <oauth2/storage/IUserRepository.h>
#include <oauth2/storage/PostgresRepositoryBase.h>

#include <memory>

namespace oauth2
{

/**
 * @brief PostgreSQL implementation of IUserRepository.
 */
class PostgresUserRepository : public IUserRepository,
                               public PostgresRepositoryBase,
                               public std::enable_shared_from_this<PostgresUserRepository>
{
  public:
    PostgresUserRepository() = default;

    void getUserInfo(const std::string &userId, OptionalJsonCallback &&cb) override;
    void getUserInfo(int32_t internalUserId, OptionalJsonCallback &&cb) override;
};

}  // namespace oauth2
