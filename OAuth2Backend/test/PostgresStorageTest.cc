
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include "PostgresOAuth2Storage.h"
#include <future>

using namespace oauth2;

DROGON_TEST(PostgresStorageTest)
{
    // Check DB availability
    auto client = drogon::app().getDbClient();
    if (!client)
    {
        LOG_WARN
            << "DB client not available. Skipping Postgres integration tests.";
        return;
    }

    // 1. Setup
    auto storage = std::make_shared<PostgresOAuth2Storage>();
    storage->initFromConfig(Json::Value());  // Init with default client

    // 2. Auth Code Flow Integration
    OAuth2AuthCode code;
    code.code = "test_pg_code_123";
    code.clientId = "vue-client";  // Use seeded client
    code.userId = "user_pg";
    code.scope = "write";
    code.redirectUri = "http://localhost/cb";
    code.expiresAt = std::time(nullptr) + 60;
    code.used = false;

    // Save
    {
        std::promise<void> p;
        auto f = p.get_future();
        storage->saveAuthCode(code, [&]() { p.set_value(); });
        f.get();
        LOG_INFO << "Postgres: Saved Auth Code";
    }

    // Delay to ensure db consistency (optional/hacky but good for diagnosis)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Get
    {
        std::promise<std::optional<OAuth2AuthCode>> p;
        auto f = p.get_future();
        storage->getAuthCode("test_pg_code_123",
                             [&](auto c) { p.set_value(c); });
        auto c = f.get();
        if (!c.has_value())
        {
            LOG_ERROR << "Postgres: Failed to retrieve Auth Code";
        }
        CHECK(c.has_value());
        if (c)
        {
            CHECK(c->code == "test_pg_code_123");
            CHECK(c->clientId == "vue-client");
        }
        LOG_INFO << "Postgres: Retrieved Auth Code";
    }

    // Cleanup (Optional but good practice)
    // Cleanup (Synchronous wait)
    {
        std::promise<void> p;
        auto f = p.get_future();
        client->execSqlAsync(
            "DELETE FROM oauth2_codes WHERE code = $1",
            [&](const drogon::orm::Result &) { p.set_value(); },
            [&](const drogon::orm::DrogonDbException &e) {
                LOG_ERROR << "Postgres Cleanup Failed: " << e.base().what();
                p.set_value();
            },
            "test_pg_code_123");
        f.get();
        LOG_INFO << "Postgres: Cleaned up test data";
    }
}
