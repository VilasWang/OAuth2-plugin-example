#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <filesystem>
#include <fulla/common/config/ConfigManager.h>
#include <fulla/drogon/plugin/OAuth2Plugin.h>
#include <cstdlib>

// ============================================================================
// Database-Agnostic Tests (Run in all storage modes)
// ============================================================================

DROGON_TEST(Unit_P1_ConfigMigration_Legacy_MainCcConfigLoadWorks)
{
    // Test that main.cc can use ConfigManager
    std::string configPath = "./config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../../config.json";

    Json::Value config;
    CHECK(fulla::common::config::ConfigManager::load(configPath, config));

    std::string errMsg;
    CHECK(fulla::common::config::ConfigManager::validate(config, errMsg));

    // Verify key config sections exist
    CHECK(config.isMember("db_clients"));
    CHECK(config.isMember("redis_clients"));
}

// ============================================================================
// Database-Dependent Tests (Skipped in memory storage mode)
// ============================================================================

DROGON_TEST(Unit_P1_ConfigMigration_Legacy_Database_EnvOverridesWorkAsBefore)
{
    // Skip this test in memory storage mode (no db_clients configured)
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        return;
    }

    // Test that environment variable overrides work consistently
#ifdef _WIN32
    _putenv_s("FULLA_DB_HOST", "test-host");
    _putenv_s("FULLA_DB_PORT", "5433");
#else
    setenv("FULLA_DB_HOST", "test-host", 1);
    setenv("FULLA_DB_PORT", "5433", 1);
#endif

    std::string configPath = "./config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../../config.json";

    Json::Value config;
    fulla::common::config::ConfigManager::load(configPath, config);

    std::string host =
      fulla::common::config::ConfigManager::get<std::string>(config, "db_clients.0.host");
    int port = fulla::common::config::ConfigManager::get<int>(config, "db_clients.0.port");

    CHECK(host == "test-host");
    CHECK(port == 5433);

#ifdef _WIN32
    _putenv_s("FULLA_DB_HOST", "");
    _putenv_s("FULLA_DB_PORT", "");
#else
    unsetenv("FULLA_DB_HOST");
    unsetenv("FULLA_DB_PORT");
#endif
}
