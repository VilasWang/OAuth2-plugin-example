
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include "RedisOAuth2Storage.h"
#include <future>

using namespace oauth2;

DROGON_TEST(RedisStorageTest)
{
    auto client = drogon::app().getRedisClient("default");
    if (!client)
    {
        LOG_WARN
            << "Redis client not available. Skipping Redis integration tests.";
        return;
    }

    // 1. Setup
    auto storage = std::make_shared<RedisOAuth2Storage>();
    // RedisStorage doesn't necessarily need initFromConfig if using global
    // drogon redis client, but looking at valid implementation, it takes config
    // only for creation. The constructor uses "default" client name.

    // 2. Auth Code Flow Integration
    OAuth2AuthCode code;
    code.code = "test_redis_code_123";
    code.clientId = "vue-client";  // Must exist or we mimic it
    code.userId = "user_redis";
    code.scope = "read";
    code.redirectUri = "http://localhost/cb";
    code.expiresAt = std::time(nullptr) + 60;
    code.used = false;

    // Save
    {
        std::promise<void> p;
        auto f = p.get_future();
        storage->saveAuthCode(code, [&]() { p.set_value(); });
        f.get();
        LOG_INFO << "Redis: Saved Auth Code";
    }

    // Get
    {
        std::promise<std::optional<OAuth2AuthCode>> p;
        auto f = p.get_future();
        storage->getAuthCode("test_redis_code_123",
                             [&](auto c) { p.set_value(c); });
        auto c = f.get();
        if (!c.has_value())
        {
            LOG_ERROR << "Redis: Failed to retrieve Auth Code";
        }
        CHECK(c.has_value());
        if (c)
        {
            CHECK(c->code == "test_redis_code_123");
            CHECK(c->clientId == "vue-client");
        }
        LOG_INFO << "Redis: Retrieved Auth Code";
    }

    // Cleanup
    {
        std::promise<void> p;
        auto f = p.get_future();
        client->execCommandAsync([](const drogon::nosql::RedisResult &r) {},
                                 [&](const std::exception &e) {},
                                 "DEL oauth2:code:test_redis_code_123");
        // We don't strictly wait here because Redis is fast, but better to
        // wait? Actually execCommandAsync doesn't block. Let's rely on fast
        // execution or just fire and forget if acceptable, BUT strict cleanup
        // requires waiting. Since we don't have a callback to set promise in
        // the simple signature above (wait, I need callback signature)

        client->execCommandAsync(
            [&](const drogon::nosql::RedisResult &r) { p.set_value(); },
            [&](const std::exception &e) { p.set_value(); },
            "DEL oauth2:code:test_redis_code_123");
        f.get();
        LOG_INFO << "Redis: Cleaned up test keys";
    }
}
