#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of
// MemoryOAuth2Storage into per-aggregate implementation files, mirroring the
// Task 9 Postgres split. This one implements ISubjectMappingRepository
// (REPOSITORY_MAPPING.md #19-21). It is ADDITIVE: MemoryOAuth2Storage /
// IOAuth2Storage are untouched and remain the production path used by
// OAuth2Plugin.cc and existing tests today.
//
// Physical location / namespace note (mirrors ISubjectMappingRepository.h's
// own header comment): this repository is conceptually identity-domain, but
// Task 10 does not move it to libs/identity (that is M2.5 / Task 19). It
// stays under OAuth2Plugin/{include,src}/oauth2/storage/ in namespace
// `oauth2`, exactly like PostgresSubjectMappingRepository/
// RedisSubjectMappingRepository.
//
// State ownership (see MemoryClientRepository.h header comment for the
// general rationale): this class owns `subjectMappings_` -- the only map
// ISubjectMappingRepository's methods touch -- and its own private mutex.
#include <oauth2/storage/ISubjectMappingRepository.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace oauth2
{

/**
 * @brief In-memory implementation of ISubjectMappingRepository.
 *
 * Faithful port of MemoryOAuth2Storage's "provider:subject" ->
 * internal_user_id map lookups/inserts.
 *
 * createUserForExternalLogin() is NOT overridden here -- it keeps the base
 * interface's default ("not supported", returns nullopt), exactly as
 * MemoryOAuth2Storage never overrode
 * IOAuth2Storage::createUserForExternalLogin either.
 */
class MemorySubjectMappingRepository : public ISubjectMappingRepository
{
  public:
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
    // MemoryOAuth2Storage's original (never overrode it either).

  private:
    std::recursive_mutex mutex_;
    // Subject mapping: "provider:subject" -> internal_user_id
    std::unordered_map<std::string, int32_t> subjectMappings_;
};

}  // namespace oauth2
