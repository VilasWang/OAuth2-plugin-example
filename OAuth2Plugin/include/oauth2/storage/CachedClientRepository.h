#pragma once

// M1 cache decorator re-architecture (design.md §7.4, evaluation H3/A1,
// authforge-sdk-refactor Task 11). Supersedes the "wrap the whole god
// interface" shape of CachedOAuth2Storage (OAuth2Plugin/include/oauth2/
// storage/CachedOAuth2Storage.h, still the production cache path today) with a
// per-repository decorator that wraps only one repository interface.
// IClientRepository is the interface design.md §7.4 explicitly calls out as the
// first target: client lookups are read-heavy and safely cacheable (no
// strong-consistency requirement like tokens/auth codes), so this decorator
// caches getClient() with the same 60s TTL convention as
// CachedOAuth2Storage::clientCache_ and leaves validateClient() as a
// pass-through (mirroring CachedOAuth2Storage, which also never cached
// validateClient()).
//
// Phase 4.4 (authforge-sdk-refactor): now implements the NEW Domain-layer
// interface authforge::oauth2::repository::IClientRepository (+ the
// authforge::oauth2::model::* DTOs) instead of the legacy oauth2 one. Types are
// qualified below (not aliased into this namespace) because the legacy
// oauth2::* DTOs still coexist in IOAuth2Storage.h until phase 4.7 deletes the
// god facade.
#include <authforge/oauth2/repository/IClientRepository.h>
#include <authforge/oauth2/model/Client.h>
#include <drogon/CacheMap.h>
#include <memory>

namespace oauth2
{

// Task 27.5: now implements the NEW Domain-layer interface
// authforge::oauth2::repository::IClientRepository.
using CachedClientRepositoryBase = ::authforge::oauth2::repository::IClientRepository;

/**
 * @brief Decorator for IClientRepository that adds an L1 in-memory cache for
 * getClient().
 *
 * Lifetime safety (A1: preserved, NOT a bugfix): mirrors the
 * `enable_shared_from_this` + `self` capture pattern already fixed onto
 * CachedOAuth2Storage (commit `30a1d1e`, defect 1.8/1.6). This decorator
 * inherits std::enable_shared_from_this<CachedClientRepository> so its async
 * continuation in getClient() captures `auto self = shared_from_this();`,
 * keeping the host object (and its clientCache_ member) alive until the
 * in-flight callback from impl_ completes -- even if the owner resets its
 * shared_ptr to this decorator while a lookup is in flight. impl_ is also a
 * std::shared_ptr, matching CachedOAuth2Storage's "Option B" ownership design.
 */
class CachedClientRepository : public CachedClientRepositoryBase,
                               public std::enable_shared_from_this<CachedClientRepository>
{
  public:
    explicit CachedClientRepository(std::shared_ptr<CachedClientRepositoryBase> impl);

    // getClient: L1 cache-aside (hit -> return cached value; miss -> delegate
    // to impl_, then fill the cache from the callback before forwarding to
    // the caller). Cached for 60 seconds, matching
    // CachedOAuth2Storage::clientCache_'s existing TTL convention.
    void getClient(
      const std::string &clientId,
      CachedClientRepositoryBase::ClientCallback &&cb
    ) override;

    // validateClient: pass-through, not cached -- matches the existing
    // CachedOAuth2Storage behavior (client secret validation is not
    // safely cacheable/does not benefit from caching).
    void validateClient(
      const std::string &clientId,
      const std::string &clientSecret,
      CachedClientRepositoryBase::BoolCallback &&cb
    ) override;

  private:
    std::shared_ptr<CachedClientRepositoryBase> impl_;
    drogon::CacheMap<std::string, ::authforge::oauth2::model::OAuth2Client> clientCache_;
};

}  // namespace oauth2
