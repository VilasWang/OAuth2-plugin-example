// tests/integration/storage/RedisCachedClientRepositoryTest.cc
//
// #42 Phase 1 (postgres-redis-cache-design.md §5): integration tests for the
// RedisCachedClientRepository cache decorator. Verifies the cache-aside behavior
// (hit/miss/fill), the §5.5 soft-fail (null Redis client → pass-through to the
// wrapped impl), the "nullopt is NOT cached" rule (a missing client must not
// shadow a future registration), and the Wave-2 P0 cache-side validateClient
// (local secret validation on hit, pass-through on miss, DEL invalidation).
//
// These tests require a live Redis instance (the decorator's whole point is the
// L2 Redis layer). They SKIP cleanly when Redis is unavailable — same convention
// as tests/contract/ContractFixtures.h's getRedisClientOrNull() (a missing
// client returns nullptr, the DROGON_TEST body returns early with zero
// assertions = skipped/passed, not failed).
//
// The wrapped IClientRepository is a counting fake (defined inline below) so the
// tests can assert exactly how many times the backing impl was consulted — the
// definitive signal that a cache hit occurred (backing count did NOT increment).
//
// NOTE on async test mechanics: the decorator's Redis calls are asynchronous
// (drogon::nosql::RedisClient::execCommandAsync). Inside a DROGON_TEST body the
// drogon event loop is running (the test framework drives it), so the async
// callbacks DO fire. waitForValue/waitForVoid (from ContractFixtures.h) block
// the test thread on a promise/future pair until the callback fires, exactly
// like every other storage integration test in this suite. Between the first
// (miss → fill) and second (hit) getClient calls, a short sleep gives the
// fire-and-forget SET time to land in Redis before the GET — this is the
// unavoidable reality of an async cache-fill without a synchronous SET path.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/nosql/RedisClient.h>
#include <drogon/utils/Utilities.h>

#include <authforge/oauth2/repository/IClientRepository.h>
#include <authforge/oauth2/model/Dto.h>
#include <authforge/storage/redis/RedisCachedClientRepository.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

using OAuth2Client = authforge::oauth2::model::OAuth2Client;
using ClientType = authforge::oauth2::model::ClientType;
using IClientRepository = authforge::oauth2::repository::IClientRepository;
using RedisCachedClientRepository = authforge::storage::redis::RedisCachedClientRepository;

namespace
{

// ---------------------------------------------------------------------------
// Counting fake IClientRepository — records every getClient/validateClient
// call so the tests can distinguish a cache hit (count unchanged) from a miss
// (count incremented). Mirrors the FakeClientRepo pattern in
// libs/oauth2/test/TokenServiceTest.cc, kept minimal (only the fields the
// decorator serializes + the test asserts on).
// ---------------------------------------------------------------------------
class CountingFakeClientRepo : public IClientRepository
{
  public:
    std::atomic<int> getClientCalls{0};
    std::atomic<int> validateClientCalls{0};

    // The client this fake "stores", keyed by clientId. Populated by the test
    // before constructing the decorator.
    std::unordered_map<std::string, OAuth2Client> store;

    void getClient(const std::string &clientId, ClientCallback &&cb) override
    {
        getClientCalls++;
        auto it = store.find(clientId);
        if (it != store.end())
            cb(it->second);
        else
            cb(std::nullopt);
    }

    void validateClient(
      const std::string &clientId,
      const std::string & /*clientSecret*/,
      BoolCallback &&cb
    ) override
    {
        validateClientCalls++;
        cb(store.find(clientId) != store.end());
    }
};

// Build a populated OAuth2Client for fixture data.
OAuth2Client makeFixtureClient(const std::string &id)
{
    OAuth2Client c;
    c.clientId = id;
    c.clientType = ClientType::CONFIDENTIAL;
    c.clientSecretHash = "deadbeefhash";
    c.salt = "saltsalt";
    c.redirectUris = {"http://localhost/cb", "http://localhost/cb2"};
    c.allowedScopes = {"openid", "profile"};
    c.tokenEndpointAuthMethod = "client_secret_basic";
    return c;
}

// Promise/future waiter for the decorator's optional<OAuth2Client> callback,
// matching ContractFixtures.h's waitForValue shape (duplicated locally so this
// test file has no cross-dependency on the contract-test header). C++17: uses
// the explicit template-parameter form (not C++20 auto function parameters).
template <typename Op>
std::optional<OAuth2Client> waitForClient(Op &&op)
{
    std::promise<std::optional<OAuth2Client>> p;
    auto f = p.get_future();
    op([&p](std::optional<OAuth2Client> v) { p.set_value(std::move(v)); });
    if (f.wait_for(std::chrono::seconds(10)) == std::future_status::timeout)
        throw std::runtime_error("RedisCachedClientRepository test TIMEOUT");
    return f.get();
}

template <typename Op>
void waitForVoid(Op &&op)
{
    std::promise<void> p;
    auto f = p.get_future();
    op([&p]() { p.set_value(); });
    if (f.wait_for(std::chrono::seconds(10)) == std::future_status::timeout)
        throw std::runtime_error("RedisCachedClientRepository test TIMEOUT");
    f.get();
}

// Waiter for a bool callback (validateClient's BoolCallback shape).
template <typename Op>
bool waitForBool(Op &&op)
{
    std::promise<bool> p;
    auto f = p.get_future();
    op([&p](bool v) { p.set_value(v); });
    if (f.wait_for(std::chrono::seconds(10)) == std::future_status::timeout)
        throw std::runtime_error("RedisCachedClientRepository test TIMEOUT");
    return f.get();
}

// Returns the configured "default" Redis client, or nullptr if unavailable
// (memory-only CI leg / no Redis running). Callers `if (!redis) return;` to SKIP.
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

// Clean any leftover cache key for a clientId so tests start from a known
// empty-cache state (a prior test run may have left an entry).
void evictCacheKey(const std::string &clientId)
{
    auto redis = getRedisOrNull();
    if (!redis)
        return;
    waitForVoid([&](auto cb) {
        redis->execCommandAsync(
          [cb](const ::drogon::nosql::RedisResult &) { cb(); },
          [cb](const ::drogon::nosql::RedisException &) { cb(); },
          "DEL authforge:cache:client:%s",
          clientId.c_str()
        );
    });
}

}  // namespace

// ===========================================================================
// Test 1: Cache miss on first read → delegates to impl; second read is a HIT
// (impl call count does NOT increment). Verifies the full cache-aside cycle:
// GET miss → delegate to impl → fire-and-forget SET fill → GET hit.
// ===========================================================================
DROGON_TEST(Integration_P1_Storage_RedisCachedClientRepository_CacheMissThenHit)
{
    auto redis = getRedisOrNull();
    if (!redis)
    {
        LOG_INFO << "[skip] Redis unavailable — RedisCachedClientRepository test skipped";
        return;
    }

    const std::string clientId = "cache-test-hit-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    evictCacheKey(clientId);

    auto fake = std::make_shared<CountingFakeClientRepo>();
    fake->store[clientId] = makeFixtureClient(clientId);

    auto decorator = std::make_shared<RedisCachedClientRepository>(fake, redis);

    // First read: MISS → delegates to impl (getClientCalls becomes 1) + fills cache.
    auto first = waitForClient([&](auto cb) { decorator->getClient(clientId, std::move(cb)); });
    REQUIRE(first.has_value());
    CHECK(first->clientId == clientId);
    CHECK(first->clientType == ClientType::CONFIDENTIAL);
    CHECK(first->redirectUris.size() == 2u);
    CHECK(first->allowedScopes.size() == 2u);
    CHECK(fake->getClientCalls.load() == 1);

    // Give the fire-and-forget SET time to land before the GET.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Second read: HIT → does NOT touch the backing impl (count stays at 1).
    auto second = waitForClient([&](auto cb) { decorator->getClient(clientId, std::move(cb)); });
    REQUIRE(second.has_value());
    CHECK(second->clientId == clientId);
    // Round-trip fidelity: the cached value matches what the fake returned.
    CHECK(second->clientSecretHash == "deadbeefhash");
    CHECK(second->salt == "saltsalt");
    CHECK(second->tokenEndpointAuthMethod == "client_secret_basic");
    CHECK(fake->getClientCalls.load() == 1);  // ← the cache-hit assertion

    evictCacheKey(clientId);
}

// ===========================================================================
// Test 2: Soft-fail — a null Redis client makes the decorator a pure
// pass-through. The backing impl is consulted on every call (no caching).
// This is the §5.5 "Redis outage must never cause a wrong answer" guarantee.
// ===========================================================================
DROGON_TEST(Integration_P1_Storage_RedisCachedClientRepository_NullRedis_PassThrough)
{
    auto fake = std::make_shared<CountingFakeClientRepo>();
    const std::string clientId = "cache-test-nullredis";
    fake->store[clientId] = makeFixtureClient(clientId);

    // Null Redis client → decorator degrades to pass-through.
    auto decorator = std::make_shared<RedisCachedClientRepository>(fake, nullptr);

    auto result = waitForClient([&](auto cb) { decorator->getClient(clientId, std::move(cb)); });
    REQUIRE(result.has_value());
    CHECK(result->clientId == clientId);
    CHECK(fake->getClientCalls.load() == 1);  // impl was consulted

    // Second call ALSO hits the impl (no cache with null Redis).
    auto result2 = waitForClient([&](auto cb) { decorator->getClient(clientId, std::move(cb)); });
    REQUIRE(result2.has_value());
    CHECK(fake->getClientCalls.load() == 2);  // impl consulted again — no cache
}

// ===========================================================================
// Test 3: A missing client (nullopt) is NOT cached. If it were, a future
// registration would be shadowed by the stale negative entry. The decorator
// must delegate to impl on every read of a nonexistent client.
// ===========================================================================
DROGON_TEST(Integration_P1_Storage_RedisCachedClientRepository_MissingClient_NotCached)
{
    auto redis = getRedisOrNull();
    if (!redis)
    {
        LOG_INFO << "[skip] Redis unavailable — RedisCachedClientRepository test skipped";
        return;
    }

    const std::string missingId = "cache-test-missing-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    evictCacheKey(missingId);

    auto fake = std::make_shared<CountingFakeClientRepo>();
    // NOTE: no entry for missingId — getClient returns nullopt.

    auto decorator = std::make_shared<RedisCachedClientRepository>(fake, redis);

    // First read: nullopt.
    auto first = waitForClient([&](auto cb) { decorator->getClient(missingId, std::move(cb)); });
    CHECK(!first.has_value());
    CHECK(fake->getClientCalls.load() == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Second read: STILL delegates to impl (the nullopt was NOT cached).
    auto second = waitForClient([&](auto cb) { decorator->getClient(missingId, std::move(cb)); });
    CHECK(!second.has_value());
    CHECK(fake->getClientCalls.load() == 2);  // ← not cached: impl consulted again
}

// ===========================================================================
// Test 4: validateClient (Wave-2 P0, docs/performance-optimization/
// optimization-wave-2-plan.md): on a cache HIT the secret is validated
// locally against the cached row — semantically identical to the Postgres
// path (PUBLIC accepted, CONFIDENTIAL empty rejected, sha256(secret+salt)
// lowercase-normalized, constant-time compare + length equality) — and the
// backing impl is NOT consulted. On a miss it passes through. A DEL (what
// the write-path invalidation hook issues after update/delete/scope
// change) sends the next validation back to the impl.
// ===========================================================================
DROGON_TEST(Integration_P1_Storage_RedisCachedClientRepository_ValidateClient_CacheSide)
{
    auto redis = getRedisOrNull();
    if (!redis)
    {
        LOG_INFO << "[skip] Redis unavailable — RedisCachedClientRepository test skipped";
        return;
    }

    const std::string clientId = "cache-test-validate";
    evictCacheKey(clientId);

    // CONFIDENTIAL client whose stored hash matches "right-secret".
    auto fake = std::make_shared<CountingFakeClientRepo>();
    fake->store[clientId] = makeFixtureClient(clientId);
    fake->store[clientId].clientSecretHash =
      ::drogon::utils::getSha256("right-secret" + fake->store[clientId].salt);

    auto decorator = std::make_shared<RedisCachedClientRepository>(fake, redis);

    // 1) Empty cache → MISS → delegates to the impl (fake returns true for
    //    stored clients regardless of the secret). The miss path does NOT
    //    fetch the row via getClient — the production flows issue their own
    //    getClient in the same request, which fills the cache.
    auto miss = waitForBool([&](auto cb) {
        decorator->validateClient(clientId, "whatever", std::move(cb));
    });
    CHECK(miss == true);
    CHECK(fake->validateClientCalls.load() == 1);
    CHECK(fake->getClientCalls.load() == 0);

    // 2) Seed the cache through getClient, wait for the async SET to land.
    waitForClient([&](auto cb) { decorator->getClient(clientId, std::move(cb)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // 3) HIT → local validation with the full semantic matrix; the backing
    //    impl counts do not move.
    auto ok = waitForBool([&](auto cb) {
        decorator->validateClient(clientId, "right-secret", std::move(cb));
    });
    CHECK(ok == true);
    auto wrong = waitForBool([&](auto cb) {
        decorator->validateClient(clientId, "wrong-secret", std::move(cb));
    });
    CHECK(wrong == false);
    auto empty = waitForBool([&](auto cb) {
        decorator->validateClient(clientId, "", std::move(cb));
    });
    CHECK(empty == false);  // CONFIDENTIAL + empty secret is rejected
    CHECK(fake->validateClientCalls.load() == 1);
    CHECK(fake->getClientCalls.load() == 1);

    // 4) Uppercase stored hash still matches (case-insensitive comparison).
    const std::string upperId = clientId + "-upper";
    evictCacheKey(upperId);
    fake->store[upperId] = makeFixtureClient(upperId);
    {
        auto h = ::drogon::utils::getSha256("right-secret" + fake->store[upperId].salt);
        std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) {
            return static_cast<char>(::toupper(c));
        });
        fake->store[upperId].clientSecretHash = h;
    }
    waitForClient([&](auto cb) { decorator->getClient(upperId, std::move(cb)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto upper = waitForBool([&](auto cb) {
        decorator->validateClient(upperId, "right-secret", std::move(cb));
    });
    CHECK(upper == true);

    // 5) PUBLIC client is accepted without any secret.
    const std::string pubId = clientId + "-public";
    evictCacheKey(pubId);
    fake->store[pubId] = makeFixtureClient(pubId);
    fake->store[pubId].clientType = ClientType::PUBLIC;
    waitForClient([&](auto cb) { decorator->getClient(pubId, std::move(cb)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto pub = waitForBool([&](auto cb) {
        decorator->validateClient(pubId, "", std::move(cb));
    });
    CHECK(pub == true);

    // 6) Invalidation: DEL the cached row (exactly what the write-path hook
    //    does) → the next validation delegates to the impl again.
    evictCacheKey(clientId);
    auto postDel = waitForBool([&](auto cb) {
        decorator->validateClient(clientId, "right-secret", std::move(cb));
    });
    CHECK(postDel == true);  // impl path (fake has the client stored)
    CHECK(fake->validateClientCalls.load() == 2);

    evictCacheKey(upperId);
    evictCacheKey(pubId);
}
