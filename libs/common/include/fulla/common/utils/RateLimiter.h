#pragma once

// F-018: process-wide, in-memory, sliding-window rate limiter.
//
// Counts FAILURES per (key) within a rolling window. When the failure count
// for a key reaches the configured threshold, subsequent checks for that key
// fail (429) until the window rolls off. A successful request resets the
// counter for its key (RFC 6749 §5.2 has no rate-limit error code, so callers
// return HTTP 429 + an OAuth2-style {error,error_description} body).
//
// Design notes:
//  - FAILURE-only counting (not request counting) so legitimate load -- and
//    especially the integration-test suite, which makes many sequential token
//    requests -- never gets throttled. Only repeated auth/validation failures
//    trip the limit (matches the audit-finding intent: brute-force / token
//    probing protection).
//  - Thread-safe: a single std::mutex guards the counter map (the token
//    endpoint is hit concurrently by Drogon's worker threads).
//  - Sliding window: each entry stores the per-second bucket of failure
//    timestamps; on each check we drop timestamps older than `windowSeconds`
//    before counting. This gives a true rolling window (not a fixed bucket
//    edge that resets all at once).
//  - Single shared instance: `RateLimiter::instance()` returns a process-wide
//    singleton (function-local static), so all four protected endpoints
//    (token / introspect / revoke / device-code polling) share one counter
//    map keyed per (ip, client_id).
//
// This header is framework-free (std only) so it can be unit-tested without
// Drogon and linked from both the controllers and the test binary.

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fulla::common::utils
{

// Configuration for the rate limiter. Defaults are intentionally lenient so
// the integration test suite (which performs many sequential successful token
// requests) is unaffected: 30 failures per (ip+client_id) per 60s window.
struct RateLimiterConfig
{
    std::size_t maxFailures = 30;     // threshold before 429 is returned
    std::chrono::seconds windowSeconds{60};  // rolling window length
    // Review MINOR #1 (unbounded memory growth): a hard cap on the number of
    // distinct (ip+client_id) buckets retained. A key that records one failure
    // and is never touched again would otherwise linger indefinitely; under a
    // distributed attack with many unique keys the map grows without bound.
    // When recordFailure() pushes the bucket count past this cap, a full sweep
    // drops every bucket whose entries have all aged out of the window (plus
    // empty buckets). Sized well above legitimate single-host load.
    std::size_t maxBuckets = 10000;

    static RateLimiterConfig defaults() noexcept
    {
        return RateLimiterConfig{};
    }
};

class RateLimiter
{
  public:
    // Process-wide singleton. Function-local static is thread-safe under
    // C++11+ (magic statics); all endpoints share one counter map this way.
    static RateLimiter &instance()
    {
        static RateLimiter limiter;
        return limiter;
    }

    // Reconfigure the thresholds. Reads once per process startup from
    // custom_config["auth"]["rate_limit"]; callers should invoke this only if
    // the config section is present (otherwise the built-in defaults stand).
    // Safe to call at any time (locks under the same mutex as checks).
    void configure(const RateLimiterConfig &cfg)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = cfg;
    }

    // Returns the current configuration. Useful for tests that need to save
    // and restore the original config after modifying it.
    RateLimiterConfig getConfig()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

    // Returns the seconds-remaining in the rolling window for which this key
    // is currently throttled, or 0 when the key is NOT throttled (i.e. the
    // caller may proceed). Callers emit HTTP 429 with `Retry-After: <n>` when
    // this returns > 0.
    //
    // NOTE: this does NOT itself record a failure -- it only reports whether
    // the key is currently over threshold. The intended usage is:
    //     if (auto retry = limiter.checkThrottled(key)) { /* emit 429 */ }
    //     ... attempt the request ...
    //     on failure: limiter.recordFailure(key);
    //     on success: limiter.recordSuccess(key);
    std::chrono::seconds checkThrottled(std::string_view key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        // operator[] inserts an empty bucket for a previously-unseen key, so
        // an attacker spamming unique keys through this (the hot pre-auth
        // path) WITHOUT ever triggering a failure would grow buckets_
        // unboundedly (the prior sweep was only on recordFailure). Detect the
        // insertion and run the cap sweep here too.
        std::string keyStr(key);
        bool inserted = (buckets_.find(keyStr) == buckets_.end());
        buckets_[keyStr]; // ensure insertion (default-constructs empty deque)
        purgeOldLocked(buckets_[keyStr], now);
        if (inserted && buckets_.size() > config_.maxBuckets)
            sweepLocked(now);
        // sweepLocked may have erased our (empty) bucket, so re-look it up
        // rather than holding a reference across the sweep (heap-use-after-free).
        auto it = buckets_.find(keyStr);
        if (it == buckets_.end())
            return std::chrono::seconds(0); // swept away → not throttled
        const auto &bucket = it->second;
        if (bucket.size() >= config_.maxFailures && !bucket.empty())
        {
            // The oldest surviving failure rolls off at now+window - oldest.
            auto oldest = bucket.front();
            auto remaining = config_.windowSeconds -
                             std::chrono::duration_cast<std::chrono::seconds>(now - oldest);
            if (remaining.count() <= 0)
                return std::chrono::seconds(0);
            return remaining;
        }
        return std::chrono::seconds(0);
    }

    // Record a failed attempt for `key`. After `maxFailures` within the
    // rolling window, checkThrottled() for that key starts returning > 0.
    void recordFailure(std::string_view key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        auto &bucket = buckets_[std::string(key)];
        purgeOldLocked(bucket, now);
        bucket.push_back(now);
        // Review MINOR #1: bound memory. When the bucket count exceeds the
        // cap, sweep every bucket once and drop those with no in-window
        // failures. Triggered on recordFailure (the growth path under attack),
        // not on the hot checkThrottled path.
        if (buckets_.size() > config_.maxBuckets)
            sweepLocked(now);
    }

    // Record a successful attempt for `key`. Resets that key's failure counter
    // (matches the audit intent: a legitimate user who eventually authenticates
    // should not accumulate stale failures).
    void recordSuccess(std::string_view key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        buckets_.erase(std::string(key));
    }

    // Test helper: clears all counters. Used by integration tests that exercise
    // the throttle path so they don't trip the limit for unrelated tests.
    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        buckets_.clear();
    }

  private:
    RateLimiter() = default;

    using TimePoint = std::chrono::steady_clock::time_point;

    void purgeOldLocked(std::deque<TimePoint> &bucket, const TimePoint &now)
    {
        auto cutoff = now - config_.windowSeconds;
        while (!bucket.empty() && bucket.front() < cutoff)
            bucket.pop_front();
    }

    // Review MINOR #1 (round 2): drop every bucket whose entries have all
    // aged out of the window (or is empty). If, after that purge, the bucket
    // count STILL exceeds the cap (many distinct keys each holding in-window
    // failures under a distributed attack), evict the buckets with the OLDEST
    // most-recent failure until under cap -- those keys are least likely to
    // be re-throttled soon, so dropping them frees the most memory at the
    // lowest behavioral cost. Called from both recordFailure AND the
    // checkThrottled insert path, since the latter is the no-failure growth
    // vector an attacker can drive without ever tripping recordFailure.
    void sweepLocked(const TimePoint &now)
    {
        for (auto it = buckets_.begin(); it != buckets_.end();)
        {
            purgeOldLocked(it->second, now);
            if (it->second.empty())
                it = buckets_.erase(it);
            else
                ++it;
        }
        // Cap enforcement: if still over, evict by oldest last-failure time.
        while (buckets_.size() > config_.maxBuckets)
        {
            auto victim = buckets_.end();
            // (TimePoint::max)() -- parens defeat the Windows `max` macro.
            TimePoint victimLast = (TimePoint::max)();
            for (auto it = buckets_.begin(); it != buckets_.end(); ++it)
            {
                // bucket is non-empty here (empty ones were erased above);
                // back() is its most-recent failure.
                if (it->second.back() < victimLast)
                {
                    victimLast = it->second.back();
                    victim = it;
                }
            }
            if (victim == buckets_.end())
                break;  // nothing to evict (shouldn't happen)
            buckets_.erase(victim);
        }
    }

    std::mutex mutex_;
    RateLimiterConfig config_{RateLimiterConfig::defaults()};
    std::unordered_map<std::string, std::deque<TimePoint>> buckets_;
};

}  // namespace fulla::common::utils
