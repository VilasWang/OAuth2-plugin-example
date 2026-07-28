#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <authforge/drogon/utils/CryptoUtils.h>
#include <authforge/oauth2/model/Dto.h>
#include <future>
#include <chrono>
#include <limits>

#ifdef max
#undef max
#endif

// Phase 4.6a (authforge-sdk-refactor): the god-facade getStorage() accessor is
// gone. This test now exercises the token lifecycle (revocation / expiration /
// valid / cleanup) through the plugin's NEW split-repository forwarding methods
// (plugin->saveAccessToken / plugin->validateAccessToken) and the new model
// DTOs (authforge::oauth2::model::OAuth2AccessToken). The cleanup case can no
// longer call storage_->deleteExpiredData() directly (the god facade is gone);
// per-backend purgeExpired() is orchestrated by OAuth2CleanupService, which is
// not exposed on the plugin -- so the cleanup case is removed here (its
// coverage lives in the contract tests + the cleanup-service unit test).

using AccessToken = authforge::oauth2::model::OAuth2AccessToken;

DROGON_TEST(Integration_P0_Storage_Advanced_Works)
{
    // 1. Setup Plugin
    auto plugin = std::make_shared<OAuth2Plugin>();
    Json::Value config;
    config["storage_type"] = "memory";
    plugin->initAndStart(config);

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch()
    )
                 .count();

    // 2. Test Revocation
    {
        std::string rawToken = "revoked_token_123";
        std::string hashedToken = authforge::drogon::utils::hashToken(rawToken);

        AccessToken revokedToken;
        revokedToken.token = hashedToken;
        revokedToken.clientId = "client1";
        revokedToken.userId = "user1";
        revokedToken.expiresAt = std::numeric_limits<int64_t>::max();
        revokedToken.revoked = true;  // Key Flag

        std::promise<void> pSave;
        plugin->saveAccessToken(revokedToken, [&]() { pSave.set_value(); });
        pSave.get_future().get();

        // Validate via Plugin (passes raw token, plugin hashes internally)
        std::promise<std::shared_ptr<AccessToken>> pVal;
        plugin->validateAccessToken(rawToken, [&](std::shared_ptr<AccessToken> t) {
            pVal.set_value(t);
        });
        auto t = pVal.get_future().get();
        CHECK(t == nullptr);  // Should detect revocation
    }

    // 3. Test Expiration
    {
        std::string rawToken = "expired_token_123";
        std::string hashedToken = authforge::drogon::utils::hashToken(rawToken);

        AccessToken expiredToken;
        expiredToken.token = hashedToken;
        expiredToken.clientId = "client1";
        expiredToken.userId = "user1";
        expiredToken.expiresAt = now - 100;  // Expired 100s ago
        expiredToken.revoked = false;

        std::promise<void> pSave;
        plugin->saveAccessToken(expiredToken, [&]() { pSave.set_value(); });
        pSave.get_future().get();

        // Validate via Plugin (passes raw token)
        std::promise<std::shared_ptr<AccessToken>> pVal;
        plugin->validateAccessToken(rawToken, [&](std::shared_ptr<AccessToken> t) {
            pVal.set_value(t);
        });
        auto t = pVal.get_future().get();
        CHECK(t == nullptr);  // Should detect expiration
    }

    // 4. Test Valid Token (Control)
    {
        std::string rawToken = "valid_token_123";
        std::string hashedToken = authforge::drogon::utils::hashToken(rawToken);

        AccessToken validToken;
        validToken.token = hashedToken;
        validToken.clientId = "client1";
        validToken.userId = "user1";
        validToken.expiresAt = now + 100;
        validToken.revoked = false;

        std::promise<void> pSave;
        plugin->saveAccessToken(validToken, [&]() { pSave.set_value(); });
        pSave.get_future().get();

        std::promise<std::shared_ptr<AccessToken>> pVal;
        plugin->validateAccessToken(rawToken, [&](std::shared_ptr<AccessToken> t) {
            pVal.set_value(t);
        });
        auto t = pVal.get_future().get();
        CHECK(t != nullptr);
        CHECK(t->token == hashedToken);
    }
}
