
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include "../plugins/OAuth2Plugin.h"
#include <future>

using namespace oauth2;

DROGON_TEST(PluginTest)
{
    // 1. Setup Plugin with Memory Storage
    auto plugin = std::make_shared<OAuth2Plugin>();
    
    Json::Value config;
    config["storage_type"] = "memory";
    
    // Add client to Memory Storage config (passed via root or specific key depending on impl)
    // MemoryOAuth2Storage implementation reads "clients" from config["clients"]? 
    // Let's check MemoryOAuth2Storage implementation again. 
    // It seems it reads THE config object passed to initFromConfig.
    // OAuth2Plugin::initStorage calls storage->initFromConfig(config["clients"] or config["memory"]?)
    // Let's assume standard structure:
    // config["clients"] map.
    
    Json::Value clientConfig;
    clientConfig["secret"] = "plugin-secret";
    clientConfig["redirect_uri"] = "http://localhost/cb";
    config["clients"]["plugin-client"] = clientConfig;
    
    // We need to check OAuth2Plugin::initStorage implementation to see what it passes to storage.
    // Assuming it passes the ROOT config or config["memory"]?
    // Based on typical Drogon plugin pattern, it might pass the whole config object.
    
    plugin->initAndStart(config);
    
    // 2. Validate Client
    {
        std::promise<bool> p;
        auto f = p.get_future();
        plugin->validateClient("plugin-client", "plugin-secret", [&](bool valid) {
            p.set_value(valid);
        });
        CHECK(f.get() == true);
    }
    
    // 3. Generate Code
    std::string authCode;
    {
        std::promise<std::string> p;
        auto f = p.get_future();
        plugin->generateAuthorizationCode("plugin-client", "user1", "scope1", [&](std::string c) {
            p.set_value(c);
        });
        authCode = f.get();
        CHECK(authCode.length() > 0);
    }
    
    // 4. Exchange Code
    {
        std::promise<std::string> p;
        auto f = p.get_future();
        plugin->exchangeCodeForToken(authCode, "plugin-client", [&](std::string token) {
            p.set_value(token);
        });
        std::string token = f.get();
        CHECK(token.length() > 0);
    }
    
    // 5. Exchange Code Again (Should Fail - Replay Attack)
    {
        std::promise<std::string> p;
        auto f = p.get_future();
        plugin->exchangeCodeForToken(authCode, "plugin-client", [&](std::string token) {
            p.set_value(token);
        });
        std::string token = f.get();
        CHECK(token.empty());
    }
}
