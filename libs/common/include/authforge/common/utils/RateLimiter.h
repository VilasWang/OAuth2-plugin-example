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

namespace authforge::common::utils
{

// Configuration for the rate limiter. Defaults are intentionally lenient so
// the integration test suite (which performs many sequential successful token
// requests) is unaffected: 30 failures per (ip+client_id) per 60s window.
struct RateLimiterConfig
{
    std::size_t maxFailures = 30;     // threshold before 429 is returned
    std::chrono::seconds windowSeconds{60};  // rolling window length

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
        auto &bucket = buckets_[std::string(key)];
        purgeOldLocked(bucket, now);
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

    std::mutex mutex_;
    RateLimiterConfig config_{RateLimiterConfig::defaults()};
    std::unordered_map<std::string, std::deque<TimePoint>> buckets_;
};

}  // namespace authforge::common::utils
