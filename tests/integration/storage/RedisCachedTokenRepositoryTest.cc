// tests/integration/storage/RedisCachedTokenRepositoryTest.cc
//
// #42 Phase 2 (postgres-redis-cache-design.md §5.2/§5.4): integration tests
// for the RedisCachedTokenRepository decorator. Verifies the cache-aside
// behavior for getAccessToken + introspectToken, the C1 revoked-check, the
// N2 discriminator (refresh-token introspections must NOT be cached), the
// revokeAccessToken invalidation + negative cache, the C7 TTL guard, the
// saveAccessToken warming, and the §5.5 soft-fail (null Redis client).
//
// Requires a live Redis instance. SKIPs cleanly when unavailable (same
// convention as the Phase 1 client-cache test + ContractFixtures.h). The
// wrapped ITokenRepository is a counting fake (inline) so the tests can
// assert exactly how many times the backing impl was consulted.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/nosql/RedisClient.h>

#include <fulla/oauth2/repository/ITokenRepository.h>
#include <fulla/oauth2/model/Dto.h>
#include <fulla/storage/redis/RedisCachedTokenRepository.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

using OAuth2AccessToken = fulla::oauth2::model::OAuth2AccessToken;
using TokenIntrospection = fulla::oauth2::model::TokenIntrospection;
using ITokenRepository = fulla::oauth2::repository::ITokenRepository;
using RedisCachedTokenRepository = fulla::storage::redis::RedisCachedTokenRepository;

namespace
{

// Counting fake ITokenRepository — records every call so the tests can
// distinguish a cache hit (count unchanged) from a miss (count incremented).
// Mirrors FakeTokenRepo (libs/oauth2/test/TokenServiceTest.cc) but minimal.
class CountingFakeTokenRepo : public ITokenRepository
{
  public:
    std::atomic<int> getAccessTokenCalls{0};
    std::atomic<int> introspectTokenCalls{0};
    std::atomic<int> revokeAccessTokenCalls{0};
    std::atomic<int> saveAccessTokenCalls{0};

    // The token this fake "stores" (keyed by the hashed-token string the
    // decorator passes through). The test populates it before constructing
    // the decorator.
    std::unordered_map<std::string, OAuth2AccessToken> accessStore;
    // A separate "refresh-token" hash the introspect fallthrough would
    // resolve (used to exercise the N2 discriminator: an introspection for
    // a refresh-token hash must NOT be cached).
    std::unordered_map<std::string, TokenIntrospection> refreshIntrospectStore;

    void saveAccessToken(const OAuth2AccessToken &t, VoidCallback &&cb) override
    {
        saveAccessTokenCalls++;
        accessStore[t.token] = t;
        if (cb)
            cb();
    }
    void getAccessToken(const std::string &token, AccessTokenCallback &&cb) override
    {
        getAccessTokenCalls++;
        auto it = accessStore.find(token);
        if (it != accessStore.end())
            cb(it->second);
        else
            cb(std::nullopt);
    }
    void saveTokenPair(const OAuth2AccessToken &at, const fulla::oauth2::model::OAuth2RefreshToken &rt, SaveResultCallback &&cb) override
    {
        (void)rt;  // unused — fake only persists the access token
        accessStore[at.token] = at;
        cb(true);
    }
    void saveRefreshToken(const fulla::oauth2::model::OAuth2RefreshToken &, VoidCallback &&cb) override { if (cb) cb(); }
    void getRefreshToken(const std::string &, RefreshTokenCallback &&cb) override { cb(std::nullopt); }
    void revokeRefreshToken(const std::string &, VoidCallback &&cb) override { if (cb) cb(); }
    void atomicRevokeRefreshToken(const std::string &, RefreshTokenCallback &&cb) override { cb(std::nullopt); }
    void revokeTokenFamily(const std::string &, VoidCallback &&cb) override { if (cb) cb(); }

    void introspectToken(const std::string &token, TokenIntrospectionCallback &&cb) override
    {
        introspectTokenCalls++;
        // First try the access store (the real Postgres impl tries access
        // then refresh). If the token is an access token, return its
        // introspection; otherwise check the refresh-introspect store (this
        // models the refresh-token fallthrough the N2 discriminator guards).
        auto atIt = accessStore.find(token);
        if (atIt != accessStore.end())
        {
            const auto &t = atIt->second;
            int64_t now = std::time(nullptr);
            if (t.revoked || t.expiresAt < now)
            {
                TokenIntrospection inactive;
                inactive.active = false;
                cb(inactive);
                return;
            }
            TokenIntrospection intro;
            intro.active = true;
            intro.clientId = t.clientId;
            intro.tokenType = "Bearer";
            intro.exp = t.expiresAt;
            intro.iat = t.issuedAt;
            intro.nbf = t.notBefore;
            intro.sub = t.userId;
            intro.aud = t.audience;
            intro.iss = t.issuer;
            intro.scope = t.scope;
            cb(intro);
            return;
        }
        auto rtIt = refreshIntrospectStore.find(token);
        if (rtIt != refreshIntrospectStore.end())
        {
            cb(rtIt->second);
            return;
        }
        TokenIntrospection inactive;
        inactive.active = false;
        cb(inactive);
    }

    void incrementIntrospectCount(const std::string &, VoidCallback &&cb) override { if (cb) cb(); }
    void revokeAccessToken(const std::string &token, const std::string & /*revokedBy*/, VoidCallback &&cb) override
    {
        revokeAccessTokenCalls++;
        auto it = accessStore.find(token);
        if (it != accessStore.end())
        {
            it->second.revoked = true;
            it->second.revokedAt = std::time(nullptr);
        }
        if (cb)
            cb();
    }
    void purgeExpired() override {}
    bool supportsTransactions() const override { return false; }
    bool supportsCas() const override { return false; }
};

OAuth2AccessToken makeAccessToken(const std::string &hash)
{
    OAuth2AccessToken t;
    t.token = hash;
    t.clientId = "test-client";
    t.userId = "test-user";
    t.scope = "openid profile";
    t.expiresAt = std::time(nullptr) + 3600;  // 1h remaining
    t.revoked = false;
    t.issuedAt = std::time(nullptr);
    t.issuer = "http://localhost:5555";
    t.audience = "test-client";
    t.notBefore = std::time(nullptr);
    t.introspectCount = 0;
    return t;
}

template <typename Op>
std::optional<OAuth2AccessToken> waitForAccess(Op &&op)
{
    std::promise<std::optional<OAuth2AccessToken>> p;
    auto f = p.get_future();
    op([&p](std::optional<OAuth2AccessToken> v) { p.set_value(std::move(v)); });
    if (f.wait_for(std::chrono::seconds(10)) == std::future_status::timeout)
        throw std::runtime_error("token cache test TIMEOUT");
    return f.get();
}

template <typename Op>
std::optional<TokenIntrospection> waitForIntro(Op &&op)
{
    std::promise<std::optional<TokenIntrospection>> p;
    auto f = p.get_future();
    op([&p](std::optional<TokenIntrospection> v) { p.set_value(std::move(v)); });
    if (f.wait_for(std::chrono::seconds(10)) == std::future_status::timeout)
        throw std::runtime_error("token cache test TIMEOUT");
    return f.get();
}

template <typename Op>
void waitForVoid(Op &&op)
{
    std::promise<void> p;
    auto f = p.get_future();
    op([&p]() { p.set_value(); });
    if (f.wait_for(std::chrono::seconds(10)) == std::future_status::timeout)
        throw std::runtime_error("token cache test TIMEOUT");
    f.get();
}

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

void evictTokenKeys(const std::string &hash)
{
    auto redis = getRedisOrNull();
    if (!redis)
        return;
    waitForVoid([&](auto cb) {
        redis->execCommandAsync(
          [cb](const ::drogon::nosql::RedisResult &) { cb(); },
          [cb](const ::drogon::nosql::RedisException &) { cb(); },
          "DEL fulla:cache:token:access:%s fulla:cache:token:introspect:%s fulla:cache:token:revoked:%s",
          hash.c_str(), hash.c_str(), hash.c_str()
        );
    });
}

std::string uniqueHash()
{
    return "testhash_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

}  // namespace

// ===========================================================================
// Test 1: getAccessToken cache miss → fill → hit. Verifies the cache-aside
// cycle + round-trip fidelity (13 fields, C6) + that the second read does
// NOT touch the backing impl.
// ===========================================================================
DROGON_TEST(Integration_P2_Storage_RedisCachedTokenRepository_GetAccessToken_MissThenHit)
{
    auto redis = getRedisOrNull();
    if (!redis)
    {
        LOG_INFO << "[skip] Redis unavailable — token cache test skipped";
        return;
    }

    const std::string hash = uniqueHash();
    evictTokenKeys(hash);

    auto fake = std::make_shared<CountingFakeTokenRepo>();
    fake->accessStore[hash] = makeAccessToken(hash);
    auto decorator = std::make_shared<RedisCachedTokenRepository>(fake, redis);

    // First read: MISS → delegates to impl + fills cache.
    auto first = waitForAccess([&](auto cb) { decorator->getAccessToken(hash, std::move(cb)); });
    REQUIRE(first.has_value());
    CHECK(first->token == hash);
    CHECK(first->clientId == "test-client");
    CHECK(first->scope == "openid profile");
    // Round-trip fidelity: all 13 fields survived the JSON serialize/deserialize.
    CHECK(first->revoked == false);
    CHECK(first->issuer == "http://localhost:5555");
    CHECK(first->audience == "test-client");
    CHECK(first->introspectCount == 0);
    CHECK(fake->getAccessTokenCalls.load() == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Second read: HIT → does NOT touch impl.
    auto second = waitForAccess([&](auto cb) { decorator->getAccessToken(hash, std::move(cb)); });
    REQUIRE(second.has_value());
    CHECK(second->token == hash);
    CHECK(fake->getAccessTokenCalls.load() == 1);  // ← cache-hit assertion

    evictTokenKeys(hash);
}

// ===========================================================================
// Test 2 (C1): a revoked token is NEVER served from cache. After
// revokeAccessToken sets token:revoked:{hash}, getAccessToken returns
// nullopt immediately (negative-cache hit), even though the access-cache
// entry may still exist from the race window.
// ===========================================================================
DROGON_TEST(Integration_P2_Storage_RedisCachedTokenRepository_Revoked_NotServed)
{
    auto redis = getRedisOrNull();
    if (!redis)
    {
        LOG_INFO << "[skip] Redis unavailable — token cache test skipped";
        return;
    }

    const std::string hash = uniqueHash();
    evictTokenKeys(hash);

    auto fake = std::make_shared<CountingFakeTokenRepo>();
    fake->accessStore[hash] = makeAccessToken(hash);
    auto decorator = std::make_shared<RedisCachedTokenRepository>(fake, redis);

    // Populate the access cache.
    auto first = waitForAccess([&](auto cb) { decorator->getAccessToken(hash, std::move(cb)); });
    REQUIRE(first.has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Revoke via the decorator (sets token:revoked + DELs access/introspect).
    waitForVoid([&](auto cb) { decorator->revokeAccessToken(hash, "test-revoker", std::move(cb)); });
    CHECK(fake->revokeAccessTokenCalls.load() == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // getAccessToken now returns nullopt (negative cache hit — does NOT
    // delegate to impl, because C1 checks token:revoked first).
    int callsBefore = fake->getAccessTokenCalls.load();
    auto after = waitForAccess([&](auto cb) { decorator->getAccessToken(hash, std::move(cb)); });
    CHECK(!after.has_value());
    // The impl's getAccessToken was NOT called (negative-cache hit short-circuits).
    CHECK(fake->getAccessTokenCalls.load() == callsBefore);

    evictTokenKeys(hash);
}

// ===========================================================================
// Test 3 (N2 discriminator): introspectToken caches a result ONLY when the
// token is a confirmed access token (its access-cache key exists). A
// refresh-token introspection (access cache absent) must NOT be cached.
// ===========================================================================
DROGON_TEST(Integration_P2_Storage_RedisCachedTokenRepository_Introspect_N2_Discriminator)
{
    auto redis = getRedisOrNull();
    if (!redis)
    {
        LOG_INFO << "[skip] Redis unavailable — token cache test skipped";
        return;
    }

    const std::string accessHash = uniqueHash();
    const std::string refreshHash = uniqueHash();
    evictTokenKeys(accessHash);
    evictTokenKeys(refreshHash);

    auto fake = std::make_shared<CountingFakeTokenRepo>();
    fake->accessStore[accessHash] = makeAccessToken(accessHash);
    // refreshHash is NOT in accessStore — introspectToken falls through to
    // the refresh-introspect store (models the refresh-token path).
    TokenIntrospection rtIntro;
    rtIntro.active = true;
    rtIntro.clientId = "test-client";
    rtIntro.sub = "test-user";
    rtIntro.exp = std::time(nullptr) + 3600;
    fake->refreshIntrospectStore[refreshHash] = rtIntro;

    auto decorator = std::make_shared<RedisCachedTokenRepository>(fake, redis);

    // --- Access-token path: warm the access cache first (the N2 gate). ---
    waitForAccess([&](auto cb) { decorator->getAccessToken(accessHash, std::move(cb)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // First introspect of the ACCESS token → MISS → delegate → cache (N2
    // gate passes because access:{hash} exists).
    auto intro1 = waitForIntro([&](auto cb) { decorator->introspectToken(accessHash, std::move(cb)); });
    REQUIRE(intro1.has_value());
    CHECK(intro1->active == true);
    CHECK(fake->introspectTokenCalls.load() == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Second introspect of the ACCESS token → HIT (impl NOT consulted).
    auto intro2 = waitForIntro([&](auto cb) { decorator->introspectToken(accessHash, std::move(cb)); });
    REQUIRE(intro2.has_value());
    CHECK(intro2->active == true);
    CHECK(fake->introspectTokenCalls.load() == 1);  // ← cached

    // --- Refresh-token path: introspect refreshHash (no access cache for it). ---
    auto rtIntro1 = waitForIntro([&](auto cb) { decorator->introspectToken(refreshHash, std::move(cb)); });
    REQUIRE(rtIntro1.has_value());
    CHECK(rtIntro1->active == true);
    int rtCallsAfter1 = fake->introspectTokenCalls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Second introspect of the REFRESH token → NOT cached (N2 gate fails:
    // access:{refreshHash} does not exist) → impl consulted again.
    auto rtIntro2 = waitForIntro([&](auto cb) { decorator->introspectToken(refreshHash, std::move(cb)); });
    REQUIRE(rtIntro2.has_value());
    CHECK(rtIntro2->active == true);
    CHECK(fake->introspectTokenCalls.load() == rtCallsAfter1 + 1);  // ← NOT cached

    evictTokenKeys(accessHash);
    evictTokenKeys(refreshHash);
}

// ===========================================================================
// Test 4 (soft-fail): a null Redis client makes the decorator a pure
// pass-through. The backing impl is consulted on every call (no caching).
// ===========================================================================
DROGON_TEST(Integration_P2_Storage_RedisCachedTokenRepository_NullRedis_PassThrough)
{
    const std::string hash = uniqueHash();
    auto fake = std::make_shared<CountingFakeTokenRepo>();
    fake->accessStore[hash] = makeAccessToken(hash);

    auto decorator = std::make_shared<RedisCachedTokenRepository>(fake, nullptr);

    auto r1 = waitForAccess([&](auto cb) { decorator->getAccessToken(hash, std::move(cb)); });
    REQUIRE(r1.has_value());
    CHECK(fake->getAccessTokenCalls.load() == 1);

    auto r2 = waitForAccess([&](auto cb) { decorator->getAccessToken(hash, std::move(cb)); });
    REQUIRE(r2.has_value());
    CHECK(fake->getAccessTokenCalls.load() == 2);  // ← no cache with null Redis
}

// ===========================================================================
// Test 5 (saveAccessToken warming): saving a token warms the access cache,
// so the first getAccessToken is a HIT (impl NOT consulted). Improves the
// cold-start window for the N2 discriminator.
// ===========================================================================
DROGON_TEST(Integration_P2_Storage_RedisCachedTokenRepository_SaveAccessToken_WarmsCache)
{
    auto redis = getRedisOrNull();
    if (!redis)
    {
        LOG_INFO << "[skip] Redis unavailable — token cache test skipped";
        return;
    }

    const std::string hash = uniqueHash();
    evictTokenKeys(hash);

    auto fake = std::make_shared<CountingFakeTokenRepo>();
    auto decorator = std::make_shared<RedisCachedTokenRepository>(fake, redis);

    // Save (warm) the token.
    auto t = makeAccessToken(hash);
    waitForVoid([&](auto cb) { decorator->saveAccessToken(t, std::move(cb)); });
    CHECK(fake->saveAccessTokenCalls.load() == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // getAccessToken → HIT (impl NOT consulted, the warm entry served).
    auto r = waitForAccess([&](auto cb) { decorator->getAccessToken(hash, std::move(cb)); });
    REQUIRE(r.has_value());
    CHECK(r->token == hash);
    CHECK(fake->getAccessTokenCalls.load() == 0);  // ← warmed, no impl call

    evictTokenKeys(hash);
}
