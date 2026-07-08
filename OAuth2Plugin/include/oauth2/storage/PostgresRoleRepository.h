#pragma once

// Task 9 (design.md §7 / REPOSITORY_MAPPING.md): split of PostgresOAuth2Storage
// into per-aggregate implementation files. This one implements
// IRoleRepository (REPOSITORY_MAPPING.md #15-16). It is ADDITIVE:
// PostgresOAuth2Storage / IOAuth2Storage are untouched and remain the
// production path used by OAuth2Plugin.cc today.
//
// Physical location / namespace note (mirrors IRoleRepository.h's own header
// comment): this repository is conceptually identity-domain, but Task 9 does
// not move it to libs/identity (that is M2.5 / Task 19). It stays under
// OAuth2Plugin/{include,src}/oauth2/storage/ in namespace `oauth2`.
#include <oauth2/storage/IRoleRepository.h>
#include <oauth2/storage/PostgresRepositoryBase.h>

#include <memory>

namespace oauth2
{

/**
 * @brief PostgreSQL implementation of IRoleRepository.
 *
 * Lifetime safety (preserved from PostgresOAuth2Storage's getUserRoles
 * overloads): both overloads' inner UserRoles-lookup continuation captures
 * `self = shared_from_this()` before doing the second Roles lookup, exactly
 * as the original did.
 */
class PostgresRoleRepository : public IRoleRepository,
                               public PostgresRepositoryBase,
                               public std::enable_shared_from_this<PostgresRoleRepository>
{
  public:
    PostgresRoleRepository() = default;

    void getUserRoles(const std::string &userId, StringListCallback &&cb) override;
    void getUserRoles(int32_t internalUserId, StringListCallback &&cb) override;
};

}  // namespace oauth2
