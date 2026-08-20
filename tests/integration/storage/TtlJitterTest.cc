// tests/integration/storage/TtlJitterTest.cc
//
// P2.1 (docs/performance-optimization-report.md bottleneck #1): unit tests for
// the TTL-stampede jitter helper. Pure-function tests — no Redis, no stack.
// The assertions are deliberately range/bucket style: the draw is random, so
// exact-value checks would be wrong, and a fixed-alpha chi-square would flake
// on correct code at that alpha. What MUST hold deterministically:
//   0 <= reduction <= ceil(0.15 * ttl), ttl_final >= 1, only-reduce,
//   ttl <= 1 passes through untouched.

#include <drogon/drogon_test.h>

#include <cmath>
#include <vector>

#include "../../libs/storage-redis/src/TtlJitter.h"

using authforge::storage::redis::applyTtlJitter;

namespace
{
constexpr int kSamples = 20000;  // enough that bucket coverage below is
                                 // statistically certain, still <10ms runtime
}

DROGON_TEST(Integration_P2_Storage_TtlJitter_Bounds)
{
    // Production TTLs: 60 (access fill), 300 (client fill), plus edges.
    for (int ttl : {1, 2, 3, 7, 60, 300})
    {
        const int maxReduction = static_cast<int>(std::ceil(ttl * 0.15));
        int observedMin = ttl;  // reduction == 0 must be reachable
        int observedMax = 0;    // reduction == maxReduction must be reachable
        for (int i = 0; i < kSamples; ++i)
        {
            const int jittered = applyTtlJitter(ttl);
            CHECK(jittered >= 1);                        // never EX 0
            CHECK(jittered <= ttl);                      // only-reduce (C7 intact)
            CHECK(jittered >= ttl - maxReduction);       // spread cap honored
            observedMin = std::min(observedMin, jittered);
            observedMax = std::max(observedMax, jittered);
        }
        if (ttl == 1)
        {
            // Pass-through: nothing to spread, value identical every call.
            CHECK(observedMin == 1);
            CHECK(observedMax == 1);
        }
        else
        {
            // Both extremes of the spread window are exercised. P(missing one
            // of maxReduction+1 buckets in 20000 draws) < (maxRed+1)·0.9^20000
            // ≈ 0 — deterministic in practice, no flake risk.
            CHECK(observedMin == ttl);                   // reduction 0 seen
            CHECK(observedMax == ttl - maxReduction);    // full reduction seen
        }
    }
}

DROGON_TEST(Integration_P2_Storage_TtlJitter_Edges)
{
    CHECK(applyTtlJitter(1) == 1);   // documented pass-through
    // ttl == 2: ceil(0.3) == 1 → outcomes {1, 2}, both reachable.
    bool sawOne = false, sawTwo = false;
    for (int i = 0; i < kSamples; ++i)
    {
        const int v = applyTtlJitter(2);
        CHECK((v == 1 || v == 2));
        sawOne = sawOne || v == 1;
        sawTwo = sawTwo || v == 2;
    }
    CHECK(sawOne);
    CHECK(sawTwo);
}
