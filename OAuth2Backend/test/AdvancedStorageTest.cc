#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include "../plugins/OAuth2Plugin.h"
#include <future>
#include <chrono>
#include <limits>

#ifdef max
#undef max
#endif

using namespace oauth2;

DROGON_TEST(AdvancedStorageTest)
{
    // 1. Setup Plugin
    auto plugin = std::make_shared<OAuth2Plugin>();
    Json::Value config;
    config["storage_type"] = "memory";
    plugin->initAndStart(config);

    auto storage = plugin->getStorage();

    // 2. Test Revocation
    {
        OAuth2AccessToken revokedToken;
        revokedToken.token = "revoked_token_123";
        revokedToken.clientId = "client1";
        revokedToken.userId = "user1";
        revokedToken.expiresAt = std::numeric_limits<int64_t>::max();
        revokedToken.revoked = true;  // Key Flag

        std::promise<void> pSave;
        storage->saveAccessToken(revokedToken, [&]() { pSave.set_value(); });
        pSave.get_future().get();

        // Validate via Plugin
        std::promise<std::shared_ptr<OAuth2AccessToken>> pVal;
        plugin->validateAccessToken("revoked_token_123",
                                    [&](std::shared_ptr<OAuth2AccessToken> t) {
                                        pVal.set_value(t);
                                    });
        auto t = pVal.get_future().get();
        CHECK(t == nullptr);  // Should detect revocation
    }

    // 3. Test Expiration
    {
        OAuth2AccessToken expiredToken;
        expiredToken.token = "expired_token_123";
        expiredToken.clientId = "client1";
        expiredToken.userId = "user1";
        // Set specific past time
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
        expiredToken.expiresAt = now - 100;  // Expired 100s ago
        expiredToken.revoked = false;

        std::promise<void> pSave;
        storage->saveAccessToken(expiredToken, [&]() { pSave.set_value(); });
        pSave.get_future().get();

        // Validate via Plugin
        std::promise<std::shared_ptr<OAuth2AccessToken>> pVal;
        plugin->validateAccessToken("expired_token_123",
                                    [&](std::shared_ptr<OAuth2AccessToken> t) {
                                        pVal.set_value(t);
                                    });
        auto t = pVal.get_future().get();
        CHECK(t == nullptr);  // Should detect expiration
    }

    // 4. Test Valid Token (Control)
    {
        OAuth2AccessToken validToken;
        validToken.token = "valid_token_123";
        validToken.clientId = "client1";
        validToken.userId = "user1";
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
        validToken.expiresAt = now + 100;
        validToken.revoked = false;

        std::promise<void> pSave;
        storage->saveAccessToken(validToken, [&]() { pSave.set_value(); });
        pSave.get_future().get();

        std::promise<std::shared_ptr<OAuth2AccessToken>> pVal;
        plugin->validateAccessToken("valid_token_123",
                                    [&](std::shared_ptr<OAuth2AccessToken> t) {
                                        pVal.set_value(t);
                                    });
        auto t = pVal.get_future().get();
        CHECK(t != nullptr);
        CHECK(t->token == "valid_token_123");
    }
}
