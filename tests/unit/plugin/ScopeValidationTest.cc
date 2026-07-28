#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <future>

// Phase 4.7b (authforge-sdk-refactor): the god MemoryOAuth2Storage local that
// this test used to construct (and never actually wire into the plugin) is
// removed. The test exercises the plugin's public scope-validation API, which
// runs against the plugin's own memory RepositoryBundle built by initAndStart().

DROGON_TEST(Unit_P0_OAuth2Plugin_ValidateClientScopes_RestrictsToAllowlist)
{
    auto plugin = std::make_shared<OAuth2Plugin>();

    Json::Value pluginConfig;
    pluginConfig["storage_type"] = "memory";
    pluginConfig["clients"]["test-client"]["secret"] = "test-secret";
    Json::Value scopesArray(Json::arrayValue);
    scopesArray.append("openid");
    scopesArray.append("profile");
    scopesArray.append("email");
    pluginConfig["clients"]["test-client"]["allowed_scopes"] = scopesArray;

    plugin->initAndStart(pluginConfig);

    // Test Case 1: Valid scopes
    {
        std::promise<std::pair<bool, std::string>> p;
        auto f = p.get_future();
        plugin->validateClientScopes(
          "test-client", {"openid", "profile"}, [&](bool success, std::string error) {
              p.set_value({success, error});
          }
        );
        auto result = f.get();
        CHECK(result.first == true);
        CHECK(result.second == "");
    }

    // Test Case 2: Invalid scope
    {
        std::promise<std::pair<bool, std::string>> p;
        auto f = p.get_future();
        plugin->validateClientScopes(
          "test-client", {"openid", "admin"}, [&](bool success, std::string error) {
              p.set_value({success, error});
          }
        );
        auto result = f.get();
        CHECK(result.first == false);
        CHECK(result.second.find("admin") != std::string::npos);
    }
}

DROGON_TEST(Unit_P0_OAuth2Plugin_ValidateUserRolesForScopes_AdminScopeProtection)
{
    auto plugin = std::make_shared<OAuth2Plugin>();

    Json::Value pluginConfig;
    pluginConfig["storage_type"] = "memory";
    plugin->initAndStart(pluginConfig);

    // Verify the static helper.
    CHECK(OAuth2Plugin::scopeRequiresAdminRole("admin") == true);
    CHECK(OAuth2Plugin::scopeRequiresAdminRole("openid") == false);
}
