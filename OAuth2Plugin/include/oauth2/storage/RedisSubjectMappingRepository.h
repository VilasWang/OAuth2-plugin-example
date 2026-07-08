#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of RedisOAuth2Storage
// into per-aggregate implementation files, mirroring the Task 9 Postgres
// split. This one implements ISubjectMappingRepository
// (REPOSITORY_MAPPING.md #19-21). It is ADDITIVE: RedisOAuth2Storage /
// IOAuth2Storage are untouched and remain the production path used by
// OAuth2Plugin.cc today.
//
// Physical location / namespace note (mirrors ISubjectMappingRepository.h's
// own header comment): this repository is conceptually identity-domain, but
// Task 10 does not move it to libs/identity (that is M2.5 / Task 19). It
// stays under OAuth2Plugin/{include,src}/oauth2/storage/ in namespace
// `oauth2`, exactly like PostgresSubjectMappingRepository from Task 9.
#include <oauth2/storage/ISubjectMappingRepository.h>
#include <oauth2/storage/RedisRepositoryBase.h>

#include <memory>

namespace oauth2
{

/**
 * @brief Redis implementation of ISubjectMappingRepository.
 *
 * createUserForExternalLogin() is NOT overridden here -- it keeps the base
 * interface's default ("not supported", returns nullopt), exactly as
 * RedisOAuth2Storage never overrode IOAuth2Storage::createUserForExternalLogin
 * either (Redis cannot mint a new user row on first external login, same as
 * Memory).
 */
class RedisSubjectMappingRepository
    : public ISubjectMappingRepository,
      public RedisRepositoryBase,
      public std::enable_shared_from_this<RedisSubjectMappingRepository>
{
  public:
    explicit RedisSubjectMappingRepository(const std::string &redisClientName = "default")
        : RedisRepositoryBase(redisClientName)
    {
    }

    void getInternalUserId(
      const std::string &subject,
      const std::string &provider,
      OptionalIntCallback &&cb
    ) override;

    void createSubjectMapping(
      const std::string &subject,
      int32_t internalUserId,
      const std::string &provider,
      BoolCallback &&cb
    ) override;

    // createUserForExternalLogin() intentionally not overridden -- keeps
    // ISubjectMappingRepository's "not supported" default, matching
    // RedisOAuth2Storage's original (never overrode it either).
};

}  // namespace oauth2
