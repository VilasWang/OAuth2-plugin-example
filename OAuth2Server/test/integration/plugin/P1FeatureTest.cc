
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <authforge/oauth2/model/Dto.h>
#include <future>

// A3: TokenIntrospection is now authforge::oauth2::model::TokenIntrospection
// (the legacy oauth2::TokenIntrospection in IOAuth2Storage.h is being deleted).
using TokenIntrospection = authforge::oauth2::model::TokenIntrospection;

DROGON_TEST(Integration_P1_Features_General_Works)
{
    // 1. Setup Plugin with Memory Storage
    auto plugin = std::make_shared<OAuth2Plugin>();

    Json::Value config;
    config["storage_type"] = "memory";

    Json::Value clientConfig;
    clientConfig["type"] = "CONFIDENTIAL";
    clientConfig["secret"] = "test-secret";
    clientConfig["redirect_uri"] = "http://localhost/cb";
    config["clients"]["test-client"] = clientConfig;

    plugin->initAndStart(config);

    // 2. Generate and Exchange for Token
    std::string accessToken;
    std::string refreshToken;
    {
        std::promise<std::string> codeP;
        auto codeF = codeP.get_future();
        plugin->generateAuthorizationCode(
          "test-client",
          "user1",
          "openid profile",
          "http://localhost/cb",
          "",
          "",
          "",  // nonce
          [&](bool success, std::string code, std::string error) {
              codeP.set_value(success ? code : "");
          }
        );
        std::string code = codeF.get();
        REQUIRE(code.length() > 0);

        std::promise<Json::Value> tokenP;
        auto tokenF = tokenP.get_future();
        plugin->exchangeCodeForToken(
          code,
          "test-client",
          "test-secret",
          "http://localhost/cb",
          "",
          [&](const Json::Value &result) { tokenP.set_value(result); }
        );
        auto result = tokenF.get();
        CHECK(result.isMember("access_token"));
        accessToken = result["access_token"].asString();
        refreshToken = result["refresh_token"].asString();
    }

    // 3. Test Token Introspection (RFC 7662)
    {
        std::promise<std::optional<TokenIntrospection>> introP;
        auto introF = introP.get_future();
        plugin->introspectToken(accessToken, [&](std::optional<TokenIntrospection> intro) {
            introP.set_value(intro);
        });
        auto intro = introF.get();
        REQUIRE(intro.has_value());
        CHECK(intro->active == true);
        CHECK(intro->clientId == "test-client");
        CHECK(intro->scope == "openid profile");
        CHECK(intro->exp > 0);
    }

    // 4. Test Token Revocation (RFC 7009)
    {
        // Revoke Access Token
        std::promise<void> revokeP;
        auto revokeF = revokeP.get_future();
        plugin->revokeAccessToken(accessToken, "test-client", [&]() { revokeP.set_value(); });
        revokeF.get();

        // Verify revoked
        std::promise<std::optional<TokenIntrospection>> introP2;
        auto introF2 = introP2.get_future();
        plugin->introspectToken(accessToken, [&](std::optional<TokenIntrospection> intro) {
            introP2.set_value(intro);
        });
        auto intro2 = introF2.get();
        // RFC 7662: active member is REQUIRED. If token is invalid, expired, or revoked, active is
        // false.
        if (intro2)
        {
            CHECK(intro2->active == false);
        }
        else
        {
            // Some implementations might return nullopt for non-existent tokens
            CHECK(true);
        }
    }

    // 5. Test Refresh Token Revocation
    {
        std::promise<void> revokeRP;
        auto revokeRF = revokeRP.get_future();
        plugin->revokeAccessToken(refreshToken, "test-client", [&]() { revokeRP.set_value(); });
        revokeRF.get();

        // Attempt to use revoked refresh token
        std::promise<Json::Value> refreshP;
        auto refreshF = refreshP.get_future();
        plugin->refreshAccessToken(refreshToken, "test-client", [&](const Json::Value &result) {
            refreshP.set_value(result);
        });
        auto result = refreshF.get();
        CHECK(result.isMember("error"));
        CHECK(result["error"].asString() == "invalid_grant");
    }
}
