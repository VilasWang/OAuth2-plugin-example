#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <filesystem>
#include <oauth2/config/ConfigManager.h>
#include <oauth2/plugin/OAuth2Plugin.h>

#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s(name, value)
#define unsetenv(name) _putenv_s(name, "")
#endif

// ============================================================================
// Database-Agnostic Tests (Run in all storage modes)
// ============================================================================

DROGON_TEST(Unit_P0_ConfigManager_Legacy_LoadValidConfig)
{
    std::string configPath = "./config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../../config.json";

    Json::Value config;
    CHECK(common::config::ConfigManager::load(configPath, config) == true);
    CHECK(config.isNull() == false);
    CHECK(config.isMember("db_clients") == true);
}

DROGON_TEST(Unit_P0_ConfigManager_Legacy_TypeSafeAccessWithDefault)
{
    Json::Value config;
    config["port"] = 8080;

    auto port = common::config::ConfigManager::get<int>(config, "port", 0);
    CHECK(port == 8080);

    auto missing = common::config::ConfigManager::get<int>(config, "missing", 123);
    CHECK(missing == 123);
}

DROGON_TEST(Unit_P0_ConfigManager_Legacy_ValidateMissingRequiredField)
{
    Json::Value config;
    // Test completely missing db_clients field (not even empty array)
    // Empty arrays are valid for memory storage mode

    std::string errMsg;
    CHECK(common::config::ConfigManager::validate(config, errMsg) == false);
    CHECK(errMsg.find("db_clients") != std::string::npos);
}

DROGON_TEST(Unit_P0_ConfigManager_Legacy_ValidatePortRange)
{
    Json::Value config;
    config["db_clients"][0]["port"] = 70000;  // Invalid port
    config["redis_clients"][0]["port"] = 65535;

    std::string errMsg;
    CHECK(common::config::ConfigManager::validate(config, errMsg) == false);
    CHECK(errMsg.find("port") != std::string::npos);
}

// ============================================================================
// Database-Dependent Tests (Skipped in memory storage mode)
// ============================================================================

DROGON_TEST(Unit_P0_ConfigManager_Legacy_Database_EnvOverrideDbHost)
{
    // Skip this test in memory storage mode (no db_clients configured)
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        return;
    }

    // Set environment variable
    setenv("OAUTH2_DB_HOST", "test-host", 1);

    std::string configPath = "./config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../../config.json";

    Json::Value config;
    CHECK(common::config::ConfigManager::load(configPath, config) == true);

    auto dbHost = common::config::ConfigManager::get<std::string>(config, "db_clients.0.host");
    CHECK(dbHost == "test-host");

    unsetenv("OAUTH2_DB_HOST");
}

// CORS allow_origins is a JSON array consumer (main.cc checks isArray()).
// The env override must split the comma-separated string into a real array,
// not clobber the node into a scalar string.
DROGON_TEST(Unit_P0_ConfigManager_EnvOverride_CorsArray)
{
    setenv("OAUTH2_CORS_ALLOW_ORIGINS", "https://a.com, https://b.com", 1);

    std::string configPath = "./config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../../config.json";

    Json::Value config;
    CHECK(common::config::ConfigManager::load(configPath, config) == true);

    const Json::Value &origins = config["custom_config"]["cors"]["allow_origins"];
    CHECK(origins.isArray() == true);
    CHECK(origins.size() == 2);
    CHECK(origins[0].asString() == "https://a.com");
    CHECK(origins[1].asString() == "https://b.com");

    unsetenv("OAUTH2_CORS_ALLOW_ORIGINS");
}

// Google redirect_uri is a scalar string consumer (GoogleController.cc).
DROGON_TEST(Unit_P0_ConfigManager_EnvOverride_GoogleRedirect)
{
    setenv("OAUTH2_GOOGLE_REDIRECT_URI", "https://prod.example.com/callback", 1);

    std::string configPath = "./config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../../config.json";

    Json::Value config;
    CHECK(common::config::ConfigManager::load(configPath, config) == true);

    auto redirect = common::config::ConfigManager::get<std::string>(
        config, "custom_config.external_auth.google.redirect_uri");
    CHECK(redirect == "https://prod.example.com/callback");

    unsetenv("OAUTH2_GOOGLE_REDIRECT_URI");
}

// Verifies the "[name=OAuth2Plugin]" named-element lookup decouples the
// OAUTH2_VUE_REDIRECT_URI override from plugin array ordering. Each config
// file inserts different plugins (Hodor/AccessLogger) so a numeric index would
// point at the wrong element; the named lookup must resolve regardless.
DROGON_TEST(Unit_P0_ConfigManager_EnvOverride_VueRedirect_ByNameLookup)
{
    setenv("OAUTH2_VUE_REDIRECT_URI", "https://prod.example.com/callback", 1);

    std::string configPath = "./config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../../config.json";

    Json::Value config;
    CHECK(common::config::ConfigManager::load(configPath, config) == true);

    auto redirect = common::config::ConfigManager::get<std::string>(
        config, "plugins[name=OAuth2Plugin].config.clients.vue-client.redirect_uri");
    CHECK(redirect == "https://prod.example.com/callback");

    unsetenv("OAUTH2_VUE_REDIRECT_URI");
}
