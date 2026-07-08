#pragma once

// Task 9 (design.md §7 / REPOSITORY_MAPPING.md): split of PostgresOAuth2Storage
// into per-aggregate implementation files. This one implements
// ISubjectMappingRepository (REPOSITORY_MAPPING.md #19-21). It is ADDITIVE:
// PostgresOAuth2Storage / IOAuth2Storage are untouched and remain the
// production path used by OAuth2Plugin.cc today.
//
// Physical location / namespace note (mirrors ISubjectMappingRepository.h's
// own header comment): this repository is conceptually identity-domain, but
// Task 9 does not move it to libs/identity (that is M2.5 / Task 19). It
// stays under OAuth2Plugin/{include,src}/oauth2/storage/ in namespace
// `oauth2`.
#include <oauth2/storage/ISubjectMappingRepository.h>
#include <oauth2/storage/PostgresRepositoryBase.h>

#include <memory>

namespace oauth2
{

/**
 * @brief PostgreSQL implementation of ISubjectMappingRepository.
 *
 * createUserForExternalLogin() is overridden here (Postgres CAN mint a new
 * user row on first external login), exactly as
 * PostgresOAuth2Storage::createUserForExternalLogin overrode the
 * IOAuth2Storage default. Memory/Redis-backed implementations of this
 * interface are expected to keep the base class's "not supported"
 * (nullopt) default (Task 10 scope, not this file).
 */
class PostgresSubjectMappingRepository
    : public ISubjectMappingRepository,
      public PostgresRepositoryBase,
      public std::enable_shared_from_this<PostgresSubjectMappingRepository>
{
  public:
    PostgresSubjectMappingRepository() = default;

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

    void createUserForExternalLogin(
      const std::string &externalId,
      const std::string &provider,
      OptionalIntCallback &&cb
    ) override;
};

}  // namespace oauth2
