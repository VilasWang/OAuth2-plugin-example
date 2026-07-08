#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of
// MemoryOAuth2Storage into per-aggregate implementation files, mirroring the
// Task 9 Postgres split. This one implements IUserRepository
// (REPOSITORY_MAPPING.md #17-18). It is ADDITIVE: MemoryOAuth2Storage /
// IOAuth2Storage are untouched and remain the production path used by
// OAuth2Plugin.cc and existing tests today.
//
// Physical location / namespace note (mirrors IUserRepository.h's own header
// comment): this repository is conceptually identity-domain, but Task 10
// does not move it to libs/identity (that is M2.5 / Task 19). It stays
// under OAuth2Plugin/{include,src}/oauth2/storage/ in namespace `oauth2`,
// exactly like PostgresUserRepository/RedisUserRepository.
//
// State ownership: unlike most of the other Memory split classes, this one
// owns NO map from the original MemoryOAuth2Storage -- the original
// getUserInfo() overloads never read from any stored map; they always
// synthesized a placeholder JSON object from the id alone. So this class has
// no private state at all.
#include <oauth2/storage/IUserRepository.h>

namespace oauth2
{

/**
 * @brief In-memory implementation of IUserRepository.
 *
 * Faithful port of MemoryOAuth2Storage's getUserInfo() overloads:
 * getUserInfo(const std::string&) tries to parse the string as a numeric
 * user id and delegates to getUserInfo(int32_t) on success, returning
 * nullopt on parse failure; getUserInfo(int32_t) synthesizes a placeholder
 * JSON object (`user_<id>` username/name/email) rather than querying a real
 * users table.
 */
class MemoryUserRepository : public IUserRepository
{
  public:
    void getUserInfo(const std::string &userId, OptionalJsonCallback &&cb) override;
    void getUserInfo(int32_t internalUserId, OptionalJsonCallback &&cb) override;
};

}  // namespace oauth2
