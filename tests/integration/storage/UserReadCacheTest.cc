// tests/integration/storage/UserReadCacheTest.cc
//
// Wave-2 P1 (docs/performance-optimization/optimization-wave-2-plan.md):
// integration tests for the src-internal UserReadCache singleton — the
// Redis cache-aside behind OAuth2Plugin::getUserRoles / getUserInfo. Covers
// the miss→fill→hit cycle, empty-roles caching, the 60s negative cache for
// absent profiles, write-path invalidation via the UserCacheInvalidator
// registry, and the disabled (null-Redis) pass-through.
//
// Requires a live Redis instance; SKIPs cleanly when unavailable (same
// convention as RedisCachedClientRepositoryTest.cc). The "backing store" is
// a counting lambda fetch so hit/miss is observable by call count.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/nosql/RedisClient.h>
#include <json/json.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// src-internal header under test (relative include — same precedent as the
// wave-1 TTL-jitter test; deliberately NOT on the public include tree).
#include "../../libs/drogon/src/UserReadCache.h"

using authforge::drogon::UserReadCache;
using authforge::drogon::UserCacheInvalidator;

namespace
{

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

void evictUserKeys(const std::string &subject)
{
    auto redis = getRedisOrNull();
    if (!redis)
        return;
    auto done = std::make_shared<std::promise<void>>();
    auto fut = done->get_future();
    redis->execCommandAsync(
      [done](const ::drogon::nosql::RedisResult &) { done->set_value(); },
      [done](const ::drogon::nosql::RedisException &) { done->set_value(); },
      "DEL authforge:cache:user:profile:%s authforge:cache:user:roles:%s",
      subject.c_str(),
      subject.c_str()
    );
    fut.wait_for(std::chrono::seconds(5));
}

template <typename Op>
std::vector<std::string> waitForRoles(Op &&op)
{
    std::promise<std::vector<std::string>> p;
    auto f = p.get_future();
    op([&p](std::vector<std::string> v) { p.set_value(std::move(v)); });
    if (f.wait_for(std::chrono::seconds(10)) != std::future_status::timeout)
        return f.get();
    throw std::runtime_error("UserReadCache test TIMEOUT (roles)");
}

template <typename Op>
std::optional<Json::Value> waitForProfile(Op &&op)
{
    std::promise<std::optional<Json::Value>> p;
    auto f = p.get_future();
    op([&p](std::optional<Json::Value> v) { p.set_value(std::move(v)); });
    if (f.wait_for(std::chrono::seconds(10)) != std::future_status::timeout)
        return f.get();
    throw std::runtime_error("UserReadCache test TIMEOUT (profile)");
}

// Register an invalidator hook equivalent to the plugin's (DEL both cache
// kinds for the subject) so the invalidation test exercises the real path.
void registerDelHook(const ::drogon::nosql::RedisClientPtr &redis)
{
    UserCacheInvalidator::instance().registerHook(
      [redis](const std::string &subject) {
          for (const char *kind : {"profile", "roles"})
          {
              std::string key = std::string("authforge:cache:user:") + kind + ":" + subject;
              redis->execCommandAsync(
                [](const ::drogon::nosql::RedisResult &) {},
                [](const ::drogon::nosql::RedisException &) {},
                "DEL %s",
                key.c_str()
              );
          }
      }
    );
}

}  // namespace

// ===========================================================================
// Test 1: roles — miss→fill→hit, then write-path invalidation goes back to
// the fetch. Empty role list is a valid cached state.
// ===========================================================================
DROGON_TEST(Integration_P2_Storage_UserReadCache_Roles_MissFillHit_Invalidate)
{
    auto redis = getRedisOrNull();
    if (!redis)
    {
        LOG_INFO << "[skip] Redis unavailable — UserReadCache test skipped";
        return;
    }

    const std::string subject = "777001";
    evictUserKeys(subject);
    UserReadCache::instance().configure(redis, 300, 120);
    registerDelHook(redis);

    auto fetchCalls = std::make_shared<std::atomic<int>>(0);
    auto fetch = [fetchCalls, subject](const std::string &s,
                                       UserReadCache::RolesCallback cb) {
        fetchCalls->fetch_add(1);
        if (s == subject)
            cb({"admin", "user"});
        else
            cb({});
    };

    // MISS → fetch (count 1) + async fill.
    auto first = waitForRoles([&](auto cb) {
        UserReadCache::instance().getRoles(subject, std::move(cb), fetch);
    });
    CHECK(first == (std::vector<std::string>{"admin", "user"}));
    CHECK(fetchCalls->load() == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // HIT → fetch not consulted.
    auto second = waitForRoles([&](auto cb) {
        UserReadCache::instance().getRoles(subject, std::move(cb), fetch);
    });
    CHECK(second == (std::vector<std::string>{"admin", "user"}));
    CHECK(fetchCalls->load() == 1);

    // Empty roles for another subject are cached as a valid state.
    const std::string emptySubject = "777002";
    evictUserKeys(emptySubject);
    auto emptyFetch = waitForRoles([&](auto cb) {
        UserReadCache::instance().getRoles(emptySubject, std::move(cb), fetch);
    });
    CHECK(emptyFetch.empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto emptyHit = waitForRoles([&](auto cb) {
        UserReadCache::instance().getRoles(emptySubject, std::move(cb), fetch);
    });
    CHECK(emptyHit.empty());
    CHECK(fetchCalls->load() == 2);  // only the first miss per subject

    // INVALIDATION (write path) → next read goes back to the fetch.
    UserCacheInvalidator::instance().invalidateUser(subject);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto postDel = waitForRoles([&](auto cb) {
        UserReadCache::instance().getRoles(subject, std::move(cb), fetch);
    });
    CHECK(postDel == (std::vector<std::string>{"admin", "user"}));
    CHECK(fetchCalls->load() == 3);

    evictUserKeys(subject);
    evictUserKeys(emptySubject);
}

// ===========================================================================
// Test 2: profile — miss→fill→hit; absent user is negative-cached; both
// subject forms are invalidated when both are known.
// ===========================================================================
DROGON_TEST(Integration_P2_Storage_UserReadCache_Profile_NegativeCache_DualForm)
{
    auto redis = getRedisOrNull();
    if (!redis)
    {
        LOG_INFO << "[skip] Redis unavailable — UserReadCache test skipped";
        return;
    }

    const std::string subject = "777003";
    const std::string publicSub = "sub_777003";
    evictUserKeys(subject);
    evictUserKeys(publicSub);
    UserReadCache::instance().configure(redis, 300, 120);
    registerDelHook(redis);

    auto fetchCalls = std::make_shared<std::atomic<int>>(0);
    auto fetch = [fetchCalls, subject](const std::string &s,
                                       UserReadCache::ProfileCallback cb) {
        fetchCalls->fetch_add(1);
        if (s == subject)
        {
            Json::Value j;
            j["id"] = 777003;
            j["username"] = "alice";
            j["email"] = "alice@example.com";
            j["email_verified"] = true;
            cb(j);
        }
        else
        {
            cb(std::nullopt);
        }
    };

    // MISS → fill; HIT round-trips the JSON.
    auto first = waitForProfile([&](auto cb) {
        UserReadCache::instance().getProfile(subject, std::move(cb), fetch);
    });
    REQUIRE(first.has_value());
    CHECK((*first)["username"].asString() == "alice");
    CHECK((*first)["email_verified"].asBool() == true);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto second = waitForProfile([&](auto cb) {
        UserReadCache::instance().getProfile(subject, std::move(cb), fetch);
    });
    REQUIRE(second.has_value());
    CHECK((*second)["id"].asInt() == 777003);
    CHECK(fetchCalls->load() == 1);

    // Absent user → nullopt, negative-cached (second read does not fetch).
    const std::string ghost = "777999";
    evictUserKeys(ghost);
    auto g1 = waitForProfile([&](auto cb) {
        UserReadCache::instance().getProfile(ghost, std::move(cb), fetch);
    });
    CHECK(!g1.has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto g2 = waitForProfile([&](auto cb) {
        UserReadCache::instance().getProfile(ghost, std::move(cb), fetch);
    });
    CHECK(!g2.has_value());
    CHECK(fetchCalls->load() == 2);  // ghost fetched once, then negative hit

    // Dual-form invalidation: DEL covers BOTH subject forms in one call.
    UserCacheInvalidator::instance().invalidateUser(subject, publicSub);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto postDel = waitForProfile([&](auto cb) {
        UserReadCache::instance().getProfile(subject, std::move(cb), fetch);
    });
    REQUIRE(postDel.has_value());
    CHECK(fetchCalls->load() == 3);  // numeric form re-fetched

    evictUserKeys(subject);
    evictUserKeys(publicSub);
    evictUserKeys(ghost);

    // Leave the singleton disabled so later suites are unaffected.
    UserReadCache::instance().configure(nullptr);
}

// ===========================================================================
// Test 3: disabled (null Redis) → pure pass-through, every read fetches.
// ===========================================================================
DROGON_TEST(Integration_P2_Storage_UserReadCache_Disabled_PassThrough)
{
    UserReadCache::instance().configure(nullptr);
    CHECK(UserReadCache::instance().enabled() == false);

    auto fetchCalls = std::make_shared<std::atomic<int>>(0);
    auto fetch = [fetchCalls](const std::string &,
                              UserReadCache::RolesCallback cb) {
        fetchCalls->fetch_add(1);
        cb({"x"});
    };
    for (int i = 0; i < 3; ++i)
    {
        auto v = waitForRoles([&](auto cb) {
            UserReadCache::instance().getRoles("123", std::move(cb), fetch);
        });
        CHECK(v == (std::vector<std::string>{"x"}));
    }
    CHECK(fetchCalls->load() == 3);
}
