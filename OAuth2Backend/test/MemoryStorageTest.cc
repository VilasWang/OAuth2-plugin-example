
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include "MemoryOAuth2Storage.h"
#include <future>

using namespace oauth2;

DROGON_TEST(MemoryStorageTest)
{
    // 1. Setup
    auto storage = std::make_shared<MemoryOAuth2Storage>();

    Json::Value config;
    config["test-client"]["secret"] = "test-secret";
    config["test-client"]["redirect_uri"] = "http://localhost/cb";

    storage->initFromConfig(config);

    // 2. Test getClient
    {
        std::promise<std::optional<OAuth2Client>> p;
        auto f = p.get_future();
        storage->getClient("test-client", [&](std::optional<OAuth2Client> c) {
            p.set_value(c);
        });
        auto client = f.get();
        CHECK(client.has_value());
        CHECK(client->clientId == "test-client");
        CHECK(client->clientSecretHash ==
              "test-secret");  // Memory stores plaintext as "hash" currently
    }

    // 3. Test validateClient
    {
        std::promise<bool> p;
        auto f = p.get_future();
        storage->validateClient("test-client", "test-secret", [&](bool valid) {
            p.set_value(valid);
        });
        CHECK(f.get() == true);
    }

    {
        std::promise<bool> p;
        auto f = p.get_future();
        storage->validateClient("test-client", "wrong-secret", [&](bool valid) {
            p.set_value(valid);
        });
        CHECK(f.get() == false);
    }

    // 4. Test Auth Code Flow
    OAuth2AuthCode code;
    code.code = "test_code_123";
    code.clientId = "test-client";
    code.userId = "user1";
    code.expiresAt = std::time(nullptr) + 60;
    code.used = false;

    // Save
    {
        std::promise<void> p;
        auto f = p.get_future();
        storage->saveAuthCode(code, [&]() { p.set_value(); });
        f.get();
    }

    // Get
    {
        std::promise<std::optional<OAuth2AuthCode>> p;
        auto f = p.get_future();
        storage->getAuthCode("test_code_123",
                             [&](std::optional<OAuth2AuthCode> c) {
                                 p.set_value(c);
                             });
        auto c = f.get();
        CHECK(c.has_value());
        CHECK(c->code == "test_code_123");
        CHECK(c->used == false);
    }

    // Mark Used
    {
        std::promise<void> p;
        auto f = p.get_future();
        storage->markAuthCodeUsed("test_code_123", [&]() { p.set_value(); });
        f.get();
    }

    // Get Again (Verify Used)
    {
        std::promise<std::optional<OAuth2AuthCode>> p;
        auto f = p.get_future();
        storage->getAuthCode("test_code_123",
                             [&](std::optional<OAuth2AuthCode> c) {
                                 p.set_value(c);
                             });
        auto c = f.get();
        CHECK(c.has_value());
        CHECK(c->used == true);
    }
}
