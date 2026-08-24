// tests/integration/storage/DelayedDoubleDeleteTest.cc
//
// #79/#80 integration tests for the shared cache-invalidation primitive
// (DelayedDoubleDelete.h): the delayed second DEL that closes the cache-aside
// refill race, and the failure observability (counter per failed DEL attempt)
// when Redis is unreachable.
//
// - The race test requires a live Redis (SKIP otherwise, same convention as
//   RedisCachedClientRepositoryTest.cc).
// - The failure test builds its own client against a closed local port, so it
//   runs regardless of the default Redis's availability.
//
// Race-test choreography (the exact #79 timeline):
//   t0  SET key <old-row>            (simulates a pre-write cache fill)
//   t1  invalidateWithDoubleDelete   (post-commit invalidation: immediate DEL)
//   t2  SET key <old-row> AGAIN      (the racing reader's fill lands AFTER the
//                                      immediate DEL -- the bug #79 describes)
//   t2+delay+margin: GET key         (must be nil: the delayed second DEL
//                                      evicted the racing refill)

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/nosql/RedisClient.h>

#include <authforge/common/ports/IMetrics.h>
#include <authforge/storage/redis/DelayedDoubleDelete.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using authforge::common::ports::IMetrics;
using authforge::common::ports::MetricLabels;
using authforge::storage::redis::invalidateWithDoubleDelete;

namespace
{

// Minimal capturing IMetrics (the shared FakeMetrics header lives under
// libs/common/testing, an include root the tests target does not carry).
// Mutation happens on the drogon loop thread (where the DEL exception
// callbacks fire); reads go through countViaLoop()/samplesViaLoop() below so
// they run on the same thread.
class CapturingMetrics : public IMetrics
{
  public:
    void incrementCounter(const std::string &name, const MetricLabels &labels, double) override
    {
        names_.push_back(name);
        kinds_.push_back(labels.at("kind"));
    }
    void setGauge(const std::string &, const MetricLabels &, double) override {}
    void observeHistogram(const std::string &, const MetricLabels &, double) override {}

    std::vector<std::string> names_;
    std::vector<std::string> kinds_;
};

::drogon::nosql::RedisClientPtr getRedisOrNull()
{
    try
    {
        return ::drogon::app().getRedisClient("default");
    }
    catch (...)
    {
        return nullptr;
    }
}

// Blocks until the framework loop fires a callback at `delaySeconds` (plus a
// small margin), proving the delayed phase elapsed without wall-clock sleeps
// drifting the schedule.
template <typename F>
void afterLoopDelay(double delaySeconds, F &&fn)
{
    std::promise<void> p;
    auto f = p.get_future();
    ::drogon::app().getLoop()->runAfter(delaySeconds, [fn, &p]() {
        fn();
        p.set_value();
    });
    if (f.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
        throw std::runtime_error("DelayedDoubleDeleteTest loop-delay TIMEOUT");
}

// Synchronous Redis GET returning the raw string or "" for nil/error.
std::string syncGet(const ::drogon::nosql::RedisClientPtr &redis, const std::string &key)
{
    std::promise<std::string> p;
    auto f = p.get_future();
    redis->execCommandAsync(
      [&p](const ::drogon::nosql::RedisResult &r) {
          if (r.type() == ::drogon::nosql::RedisResultType::kString)
              p.set_value(r.asString());
          else
              p.set_value("");  // nil
      },
      [&p](const ::drogon::nosql::RedisException &) { p.set_value(""); },
      "GET %s",
      key.c_str()
    );
    if (f.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
        throw std::runtime_error("DelayedDoubleDeleteTest GET TIMEOUT");
    return f.get();
}

void syncSet(
  const ::drogon::nosql::RedisClientPtr &redis,
  const std::string &key,
  const std::string &value
)
{
    std::promise<void> p;
    auto f = p.get_future();
    redis->execCommandAsync(
      [&p](const ::drogon::nosql::RedisResult &) { p.set_value(); },
      [&p](const ::drogon::nosql::RedisException &) { p.set_value(); },
      "SET %s %s",
      key.c_str(),
      value.c_str()
    );
    if (f.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
        throw std::runtime_error("DelayedDoubleDeleteTest SET TIMEOUT");
    f.get();
}

}  // namespace

// #79: a refill that lands AFTER the invalidation's immediate DEL (the exact
// race that pinned rotated secrets / revoked roles for a full TTL) is evicted
// by the delayed second DEL.
DROGON_TEST(Integration_P1_Storage_DelayedDoubleDelete_RacingRefill_EvictedBySecondDel)
{
    auto redis = getRedisOrNull();
    if (!redis)
    {
        CHECK(true);
        return;  // SKIP: no live Redis
    }

    const std::string key = "authforge:cache:client:test-dd-race-79";
    auto metrics = std::make_shared<CapturingMetrics>();

    // t0: pre-write fill with the OLD row.
    syncSet(redis, key, "old-row");
    CHECK(syncGet(redis, key) == "old-row");

    // t1: the invalidation (immediate DEL + delayed DEL at +2000ms -- well
    // above any plausible immediate-DEL landing latency, so the wait loop
    // below reliably observes the immediate DEL and NOT the delayed one).
    constexpr int kTestDelayMs = 2000;
    invalidateWithDoubleDelete(redis, key, metrics, "client", kTestDelayMs);

    // The immediate DEL is async on a pooled connection: WAIT for it to land
    // before staging the racing refill (otherwise the refill's SET can be
    // reordered ahead of the DEL on a different connection, which tests
    // nothing). After this loop the key is provably gone; everything we SET
    // from here on lands strictly after the immediate DEL — the exact #79
    // race precondition.
    bool immediateDelLanded = false;
    for (int i = 0; i < 100 && !immediateDelLanded; ++i)
    {
        if (syncGet(redis, key).empty())
            immediateDelLanded = true;
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(immediateDelLanded);

    // t2: the racing reader's refill of the OLD row lands AFTER the immediate
    // DEL (single-DEL semantics would pin this for the full TTL).
    syncSet(redis, key, "old-row-stale-refill");
    CHECK(syncGet(redis, key) == "old-row-stale-refill");

    // After the delayed phase: the racing refill must be gone (and the
    // failure counter untouched -- every DEL here succeeds).
    int counterAfter = 0;
    afterLoopDelay(kTestDelayMs / 1000.0 + 0.25, [&metrics, &counterAfter]() {
        // Runs on the loop thread, same as the DEL callbacks -- no race on
        // CapturingMetrics' vectors.
        counterAfter = static_cast<int>(metrics->names_.size());
    });
    CHECK(syncGet(redis, key) == "");
    CHECK(counterAfter == 0);

    // Cleanup.
    syncSet(redis, key, "");
    invalidateWithDoubleDelete(redis, key, metrics, "client", kTestDelayMs);
}

// Deterministic fault injection: a RedisClient whose every command fails
// through the exception path. (A real client pointed at a closed port does
// NOT work here -- drogon buffers commands while the connection retries, so
// the exception callbacks never fire; a stub is both deterministic and
// network-independent.)
class FailingRedisClient : public ::drogon::nosql::RedisClient
{
  public:
    void execCommandAsync(
      ::drogon::nosql::RedisResultCallback &&,
      ::drogon::nosql::RedisExceptionCallback &&exceptionCallback,
      std::string_view,
      ...
    ) noexcept override
    {
        exceptionCallback(::drogon::nosql::RedisException(
          ::drogon::nosql::RedisErrorCode::kConnectionBroken,
          "connection refused (test fault injection)"
        ));
    }

    std::shared_ptr<::drogon::nosql::RedisSubscriber> newSubscriber() noexcept override
    {
        return nullptr;
    }
    std::shared_ptr<::drogon::nosql::RedisTransaction> newTransaction() noexcept(false) override
    {
        throw ::drogon::nosql::RedisException(
          ::drogon::nosql::RedisErrorCode::kConnectionBroken, "fault injection"
        );
    }
    void newTransactionAsync(
      const std::function<void(const std::shared_ptr<::drogon::nosql::RedisTransaction> &)>
        &callback
    ) override
    {
        callback(nullptr);
    }
    void setTimeout(double) override {}
    void closeAll() override {}
};

// #80: with Redis unreachable, every failed DEL attempt is counted (4 per
// invalidation: immediate + its retry, delayed + its retry) and the helper
// still completes without throwing (soft-fail contract).
DROGON_TEST(Integration_P0_Storage_DelayedDoubleDelete_UnreachableRedis_CountsFailedAttempts)
{
    auto deadRedis = std::make_shared<FailingRedisClient>();

    auto metrics = std::make_shared<CapturingMetrics>();
    constexpr int kTestDelayMs = 100;
    // Phase 1 (immediate DEL + its retry) fails synchronously on this thread;
    // phase 2 fires on the loop at +kTestDelayMs. The promise/future join
    // below establishes the happens-before for reading the counters.
    invalidateWithDoubleDelete(deadRedis, "authforge:cache:client:dead-80", metrics, "client", kTestDelayMs);
    CHECK(metrics->names_.size() == 2);

    int attempts = 0;
    afterLoopDelay(kTestDelayMs / 1000.0 + 0.25, [&metrics, &attempts]() {
        attempts = static_cast<int>(metrics->names_.size());
    });
    CHECK(attempts == 4);
    for (std::size_t i = 0; i < metrics->names_.size(); ++i)
    {
        CHECK(metrics->names_[i] == "authforge_cache_invalidation_failures_total");
        CHECK(metrics->kinds_[i] == "client");
    }
}
