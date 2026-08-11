#pragma once

// #42 Phase 1 (postgres-redis-cache-design.md §5.1/§5.2): a Redis-backed cache
// decorator that wraps an IClientRepository (the Postgres impl in production) and
// serves hot getClient() reads from Redis, falling through to the wrapped impl on
// any miss/error/null-client (§5.5 transparent degrade). validateClient() is a
// pure pass-through: client-secret validation is not safely cacheable (a cached
// "valid" could outlive a credential rotation), matching the existing
// CachedClientRepository's decision to never cache validateClient().
//
// This is the L2 (Redis) counterpart to the L1 (in-process) CachedClientRepository
// in this same package. The two are independent decorators: CachedClientRepository
// uses drogon::CacheMap (process-local), this one uses the shared Redis pool
// (cross-instance). Combining L1+L2 is a Phase-4 optimization (design §2 non-goal).
//
// Lifetime safety: inherits std::enable_shared_from_this<RedisCachedClientRepository>
// so the async Redis continuation captures `auto self = shared_from_this();`,
// keeping this object (and its impl_ member) alive until the in-flight Redis
// callback completes — mirroring the pattern fixed onto CachedClientRepository
// (commit 30a1d1e, defect 1.8/1.6) and every other Redis repo split in this package.
//
// Correctness constraints enforced here (design §5.5/S1, §5.7/N1):
//  - The user callback fires EXACTLY ONCE even if drogon fires both the success
//    and error callbacks on a torn connection (shared_ptr<atomic<bool>> fired).
//  - Counters go through the IMetrics port (injected, currently log-emitting via
//    DrogonMetrics — NOT Prometheus), so a future PromExporter-backed impl picks
//    them up with zero decorator changes.
#include <authforge/common/ports/IMetrics.h>
#include <authforge/oauth2/repository/IClientRepository.h>

#include <drogon/nosql/RedisClient.h>

#include <atomic>
#include <memory>
#include <string>

namespace authforge::storage::redis
{

// Alias to the Domain-layer interface this decorator implements (same alias shape
// as RedisClientRepository / CachedClientRepository in this package).
using RedisCachedClientRepositoryBase = ::authforge::oauth2::repository::IClientRepository;

/**
 * @brief Redis L2 cache decorator for IClientRepository (design #42 Phase 1).
 *
 * Wraps an underlying IClientRepository (the Postgres impl in production) and
 * serves getClient() from Redis when possible. Every Redis operation soft-fails
 * to the wrapped impl on null client, miss, or error — a Redis outage never
 * causes a wrong answer or a failed request (design §5.5).
 */
class RedisCachedClientRepository : public RedisCachedClientRepositoryBase,
                                    public std::enable_shared_from_this<RedisCachedClientRepository>
{
  public:
    /**
     * @brief Construct the decorator around @p impl, using @p redisClient for the
     * cache and emitting counters through @p metrics (may be nullptr).
     *
     * @p clientTtlSeconds controls the Redis SET EX on cache fill (default 300s,
     * design §5.3). A null @p redisClient is legal: the decorator degrades to a
     * pure pass-through to @p impl (soft-fail).
     */
    RedisCachedClientRepository(
      std::shared_ptr<RedisCachedClientRepositoryBase> impl,
      ::drogon::nosql::RedisClientPtr redisClient,
      std::shared_ptr<::authforge::common::ports::IMetrics> metrics = nullptr,
      int clientTtlSeconds = 300
    );

    /// getClient: Redis cache-aside (design §5.5). Hit → deserialize + return;
    /// miss (kNil) / null client / error → delegate to impl_ with the same
    /// callback (soft-fail). The callback fires exactly once (atomic guard).
    void getClient(const std::string &clientId, ClientCallback &&cb) override;

    /// validateClient: pure pass-through to impl_. Client-secret validation is
    /// not safely cacheable (cached "valid" could outlive a credential rotation).
    void validateClient(
      const std::string &clientId,
      const std::string &clientSecret,
      BoolCallback &&cb
    ) override;

  private:
    std::shared_ptr<RedisCachedClientRepositoryBase> impl_;
    ::drogon::nosql::RedisClientPtr redisClient_;
    std::shared_ptr<::authforge::common::ports::IMetrics> metrics_;
    int clientTtlSeconds_;

    // Helper: emit a cache counter if metrics_ is wired (no-op otherwise).
    void emitMetric(const char *outcome) const;
};

}  // namespace authforge::storage::redis
