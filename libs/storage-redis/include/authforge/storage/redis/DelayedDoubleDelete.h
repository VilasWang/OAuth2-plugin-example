#pragma once

// #79/#80 (cache invalidation hardening): the single shared write-path
// invalidation primitive for the Redis cache-aside layers (client row cache,
// user profile/roles cache, token positive-entry cache).
//
// #79 -- the refill race: a reader whose Postgres read starts BEFORE an
// admin write commits can land its cache-fill SET AFTER the post-commit
// invalidation DEL, pinning the pre-change row (rotated secret / revoked
// roles) for a full cache TTL even though the DEL "succeeded". The fix is
// the classic delayed double delete:
//
//     DEL (immediate, post-commit)          -- kills the pre-write entry
//     ... delayMs ...                       -- > p99 PG-read latency
//     DEL (scheduled on the framework loop) -- kills any refill that raced
//
// #80 -- observability: every FAILED DEL attempt increments
// authforge_cache_invalidation_failures_total{kind=<kind>} via the injected
// IMetrics port and logs (WARN on first failure, ERROR after the built-in
// single immediate retry), so a Redis blip during a secret rotation is
// visible instead of LOG_DEBUG-invisible.
//
// Failure semantics: up to 4 counted attempts per invalidation with a dead
// Redis (immediate fail + its retry, delayed fail + its retry). The delayed
// DEL is scheduled regardless of the immediate phase's outcome. Losing the
// delayed DEL entirely (process death inside the delay window) degrades to
// the pre-#79 single-DEL behavior, which the cache TTL already bounds --
// soft-fail: callers never receive errors from this helper.
//
// Placement: exported from libs/storage-redis so BOTH the decorators in this
// package (RedisCachedTokenRepository) and the libs/drogon invalidation hooks
// (OAuth2Plugin's ClientCacheInvalidator/UserCacheInvalidator wiring,
// UserReadCache) share one implementation instead of drifting copies.

#include <authforge/common/ports/IMetrics.h>

#include <drogon/nosql/RedisClient.h>

#include <memory>
#include <string>

namespace authforge::storage::redis
{

/// Default second-DEL delay: comfortably above the p99 read latency of the
/// Postgres lookups whose results get refilled, small enough that a
/// straggler eviction lands well before any human notices a stale row.
///
/// Config scope note (PR #85 review): the cache.invalidation_double_delete_
/// delay_ms knob threads into the CLIENT/USER invalidation hooks only; the
/// token decorator's revoke path calls this helper with the default (its
/// negative-marker-before-DEL ordering already self-corrects racing
/// refills on the next read).
constexpr int kDefaultDoubleDeleteDelayMs = 200;

/**
 * DEL a cache key now, retry once on failure (WARN -> retry -> ERROR +
 * metric), then schedule a delayed second DEL on the Drogon event loop to
 * close the cache-aside refill race (#79). Fire-and-forget; never throws.
 *
 * @param redis   Redis client to DEL through (must outlive the call only --
 *                captured by value into the async continuations).
 * @param key     Full cache key to invalidate.
 * @param metrics Optional metrics port; when set, every failed DEL attempt
 *                increments authforge_cache_invalidation_failures_total
 *                with the {"kind": kind} label (#80).
 * @param kind    Label value identifying the cache family ("client" /
 *                "user" / "token") in logs and metrics.
 * @param delayMs Delay before the second DEL; clamped to
 *                [50, 2000], defaults to kDefaultDoubleDeleteDelayMs.
 */
void invalidateWithDoubleDelete(
  const ::drogon::nosql::RedisClientPtr &redis,
  const std::string &key,
  const std::shared_ptr<::authforge::common::ports::IMetrics> &metrics,
  std::string kind,
  int delayMs = kDefaultDoubleDeleteDelayMs
);

}  // namespace authforge::storage::redis
