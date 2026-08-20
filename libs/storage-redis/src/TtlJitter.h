#pragma once
// libs/storage-redis/src/TtlJitter.h — src-internal helper, deliberately NOT
// under include/authforge (public headers are api-diff-snapshotted; adding one
// for this would register as SDK surface drift).
//
// TTL stampede mitigation (docs/performance-optimization-report.md bottleneck
// #1): uniform request patterns fill the positive caches with identical TTLs,
// so the whole cohort expires in one wave and the next miss-storm lands on PG
// together (~800ms p99 spikes at a 30s period under pool-64). Spreading each
// fill's TTL downward by up to 15% (uniform) de-synchronizes the wave while
// keeping every invariant the callers rely on:
//
//   - only-reduce: ttl_final <= ttl, so the C7 "cache no longer than the
//     token's remaining lifetime" guard still holds unchanged;
//   - ttl_final >= 1 for every ttl >= 1: ceil(0.15 * ttl) <= ttl - 1 whenever
//     ttl >= 2, so the SET never gets EX 0 (Redis rejects it);
//   - ttl <= 1 is a pass-through (nothing meaningful to spread).
//
// Negative-cache SETs (fixed 60s) and storage_type=redis SETEX sites are
// intentionally NOT jittered: revocations are event-driven and naturally
// unaligned, and in the redis-storage mode the TTL carries expiry semantics.
//
// The engine is thread_local: these calls run on Drogon IO threads and a
// shared static mt19937 would be a data race.

#include <cmath>
#include <random>

namespace authforge::storage::redis
{

// Reduce `ttl` by a uniform amount in [0, ceil(0.15 * ttl)]; never below 1.
inline int applyTtlJitter(int ttl)
{
    if (ttl <= 1)
        return ttl;
    static thread_local std::mt19937 rng{std::random_device{}()};
    const int maxReduction = static_cast<int>(std::ceil(ttl * 0.15));
    const int reduction = std::uniform_int_distribution<int>(0, maxReduction)(rng);
    return ttl - reduction;
}

}  // namespace authforge::storage::redis
