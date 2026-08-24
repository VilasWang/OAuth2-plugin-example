// Implementation of the shared #79/#80 cache-invalidation primitive; see
// DelayedDoubleDelete.h for the design commentary (refill race, failure
// accounting, soft-fail contract).

#include <authforge/storage/redis/DelayedDoubleDelete.h>

#include <drogon/drogon.h>

#include <algorithm>

namespace authforge::storage::redis
{
namespace
{
using ::drogon::nosql::RedisClientPtr;
using ::drogon::nosql::RedisException;
using ::drogon::nosql::RedisResult;
using MetricsPtr = std::shared_ptr<::authforge::common::ports::IMetrics>;

constexpr const char *kFailureCounter = "authforge_cache_invalidation_failures_total";

void countFailedAttempt(const MetricsPtr &metrics, const std::string &kind)
{
    if (metrics)
        metrics->incrementCounter(kFailureCounter, {{"kind", kind}});
}

// One DEL attempt. On failure: count the attempt (#80 semantics -- every
// failed attempt increments), then either warn + retry once immediately, or
// log the terminal ERROR when the retry itself failed.
void attemptDel(
  const RedisClientPtr &redis,
  const std::string &key,
  const MetricsPtr &metrics,
  const std::string &kind,
  bool isRetry
)
{
    redis->execCommandAsync(
      [](const RedisResult &) {},
      [redis, key, metrics, kind, isRetry](const RedisException &e) {
          countFailedAttempt(metrics, kind);
          if (!isRetry)
          {
              LOG_WARN << "DelayedDoubleDelete: DEL failed for " << key << " (retrying once): "
                       << e.what();
              attemptDel(redis, key, metrics, kind, true);
              return;
          }
          LOG_ERROR << "DelayedDoubleDelete: DEL retry failed for " << key
                    << " (cache may serve a stale row until TTL): " << e.what();
      },
      "DEL %s",
      key.c_str()
    );
}
}  // namespace

void invalidateWithDoubleDelete(
  const RedisClientPtr &redis,
  const std::string &key,
  const std::shared_ptr<::authforge::common::ports::IMetrics> &metrics,
  std::string kind,
  int delayMs
)
{
    if (!redis || key.empty())
        return;
    const int clampedDelay = std::clamp(
      delayMs, 50, 2000
    );
    const int delay = (delayMs <= 0) ? kDefaultDoubleDeleteDelayMs : clampedDelay;

    // Phase 1: immediate post-commit DEL (with its built-in retry).
    attemptDel(redis, key, metrics, kind, false);

    // Phase 2 (#79): the delayed second DEL. Scheduled unconditionally --
    // it is the actual race defense, and it also acts as a third chance for
    // a Redis that was blipping during phase 1. Losing it to process death
    // degrades to single-DEL semantics (TTL-bounded), per the header.
    auto redisForTimer = redis;
    auto metricsForTimer = metrics;
    auto keyForTimer = key;
    auto kindForTimer = kind;
    ::drogon::app().getLoop()->runAfter(
      delay / 1000.0,
      [redisForTimer, keyForTimer, metricsForTimer, kindForTimer]() {
          attemptDel(redisForTimer, keyForTimer, metricsForTimer, kindForTimer, false);
      }
    );
}

}  // namespace authforge::storage::redis
