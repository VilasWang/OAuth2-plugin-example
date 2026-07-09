// #define DROGON_TEST_MAIN
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <authforge/drogon/controllers/HealthController.h>
#include <authforge/drogon/controllers/GoogleController.h>
#include <authforge/drogon/controllers/WeChatController.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <json/json.h>
#include <sstream>

// Test configuration constants
namespace test_config
{
constexpr int APP_STARTUP_TIMEOUT_SECONDS = 60;
constexpr int APP_PREWARM_MS = 500;
}  // namespace test_config

// Helper to parse JSON (replaces deprecated Json::Reader)
static bool parseJsonString(std::istream &stream, Json::Value &json)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    return Json::parseFromStream(builder, stream, &json, &errs);
}

// Helper to serialize JSON to string (replaces deprecated Json::StyledWriter)
static std::string jsonToStyledString(const Json::Value &json)
{
    Json::StreamWriterBuilder builder;
    return Json::writeString(builder, json);
}

// Helper to create log directory from config
void createLogDirFromConfig(const std::string &configPath)
{
    std::ifstream configFile(configPath);
    if (!configFile.is_open())
        return;

    Json::Value root;
    if (parseJsonString(configFile, root))
    {
        const auto &logConfig = root["app"]["log"];
        if (!logConfig.isNull())
        {
            std::string logPath = logConfig.get("log_path", "").asString();
            if (!logPath.empty())
            {
                // Handle relative paths in tests (relative to build dir
                // usually, or CWD)
                try
                {
                    std::filesystem::path path(logPath);
                    // Verify if we need to resolve it relative to config file
                    // location or CWD For simplicity, we assume CWD or relative
                    // to it, just like drogon does for the most part
                    if (!std::filesystem::exists(path))
                    {
                        std::filesystem::create_directories(path);
                        std::cout << "Created log directory: " << path << std::endl;
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Failed to create log directory: " << e.what() << std::endl;
                }
            }
        }
    }
}

// Helper to load config with Environment Variable overrides and write to a temp
// file
std::string loadConfigWithEnv(const std::string &configPath)
{
    Json::Value root;
    std::ifstream configFile(configPath);
    if (!configFile.is_open())
    {
        std::cerr << "Error: Config file not found: " << configPath << std::endl;
        return configPath;
    }

    if (!parseJsonString(configFile, root))
    {
        std::cerr << "Error: Failed to parse config file: " << configPath << std::endl;
        return configPath;
    }

    // Check storage type to determine if database overrides should be applied
    bool isMemoryStorage = false;
    if (root.isMember("plugins") && root["plugins"].isArray())
    {
        for (const auto &plugin : root["plugins"])
        {
            if (
              plugin.isMember("name") && plugin["name"].asString() == "OAuth2Plugin" &&
              plugin.isMember("config") && plugin["config"].isMember("storage_type")
            )
            {
                std::string storageType = plugin["config"]["storage_type"].asString();
                if (storageType == "memory")
                {
                    isMemoryStorage = true;
                    std::cout << "Memory storage detected: skipping database "
                                 "environment variable overrides"
                              << std::endl;
                }
                break;
            }
        }
    }

    // Override DB Settings (only if not memory storage and db_clients array is
    // not empty)
    if (!isMemoryStorage && root["db_clients"].isArray() && root["db_clients"].size() > 0)
    {
        if (const char *env = std::getenv("OAUTH2_DB_HOST"))
        {
            if (env[0] != '\0')
                root["db_clients"][0]["host"] = env;
        }
        if (const char *env = std::getenv("OAUTH2_DB_NAME"))
        {
            if (env[0] != '\0')
                root["db_clients"][0]["dbname"] = env;
        }
        if (const char *env = std::getenv("OAUTH2_DB_PASSWORD"))
        {
            // Always override password if env var is set, even if empty
            // This allows CI to disable password authentication
            root["db_clients"][0]["passwd"] = env;
        }
    }

    // Override Redis Settings (only if not memory storage and redis_clients
    // array is not empty)
    if (!isMemoryStorage && root["redis_clients"].isArray() && root["redis_clients"].size() > 0)
    {
        if (const char *env = std::getenv("OAUTH2_REDIS_HOST"))
        {
            if (env[0] != '\0')
                root["redis_clients"][0]["host"] = env;
        }
        if (const char *env = std::getenv("OAUTH2_REDIS_PASSWORD"))
        {
            // Always override password if env var is set, even if empty
            // This allows CI to disable password authentication
            root["redis_clients"][0]["passwd"] = env;
        }
    }

    // Override Client Secret in OAuth2Plugin
    if (const char *env = std::getenv("OAUTH2_VUE_CLIENT_SECRET"))
    {
        if (env[0] != '\0' && root["plugins"].isArray())
        {
            for (auto &plugin : root["plugins"])
            {
                if (plugin.get("name", "").asString() == "OAuth2Plugin")
                {
                    if (
                      plugin.isMember("config") && plugin["config"].isMember("clients") &&
                      plugin["config"]["clients"].isMember("vue-client")
                    )
                    {
                        plugin["config"]["clients"]["vue-client"]["secret"] = env;
                    }
                    break;
                }
            }
        }
    }

    // Disable daemonizing and relaunch features in tests, because fork() breaks
    // the std::promise cross-thread sync
    if (root.isMember("app") && root["app"].isObject())
    {
        root["app"]["relaunch_on_error"] = false;
        root["app"]["run_as_daemon"] = false;
        root["app"]["handle_sig_term"] = false;
    }

    // Write runtime config (use specific name for test to avoid conflict?)
    std::string runtimePath = "test_config_env_runtime.json";
    std::ofstream runtimeFile(runtimePath);
    runtimeFile << jsonToStyledString(root);
    runtimeFile.close();

    std::cout << "Loaded config with ENV overrides from: " << configPath << " -> " << runtimePath
              << std::endl;
    return runtimePath;
}

int main(int argc, char **argv)
{
    using namespace drogon;

    // 1. Initial configuration loading in main thread
    std::string configPath = "./config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../config.json";
    if (!std::filesystem::exists(configPath))
        configPath = "../../../config.json";

    std::string runtimeConfigPath;
    if (std::filesystem::exists(configPath))
    {
        std::cout << "Initial config search found: " << configPath << std::endl;
        createLogDirFromConfig(configPath);

        // In database storage modes, apply environment variable overrides
        runtimeConfigPath = loadConfigWithEnv(configPath);
        drogon::app().loadConfigFile(runtimeConfigPath);
    }
    else
    {
        std::cerr << "WARNING: config.json not found during pre-start check." << std::endl;
        return 1;
    }

    // EXPERIMENTAL (M3 Task 20, mechanism verification): HealthController now
    // lives in libs/drogon as a STATIC library target with AutoCreation=false,
    // so the test binary must explicitly construct and register it too, same
    // as OAuth2Server/main.cc, before app().run() is called on the thread
    // below. Without this, every /health* request in this test binary would
    // 404.
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::HealthController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::GoogleController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::WeChatController>()
    );

    std::promise<void> p1;
    std::future<void> f1 = p1.get_future();
    std::atomic<bool> signalingStarted{false};

    // 2. Start the main loop on another thread
    std::thread thr([&]() {
        try
        {
            drogon::app().registerBeginningAdvice([&]() {
                std::cout << "Drogon app ready, signaling tests to start..." << std::endl;
                // Add a small delay on macOS to ensure EventLoop and ThreadPool
                // are fully initialized before tests start hitting the server.
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                bool expected = false;
                if (signalingStarted.compare_exchange_strong(expected, true))
                {
                    p1.set_value();
                }
            });

            std::cout << "Starting drogon::app().run()..." << std::endl;
            drogon::app().run();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Exception in app().run(): " << e.what() << std::endl;
            bool expected = false;
            if (signalingStarted.compare_exchange_strong(expected, true))
            {
                p1.set_value();
            }
        }
        catch (...)
        {
            std::cerr << "Unknown exception in app().run()" << std::endl;
            bool expected = false;
            if (signalingStarted.compare_exchange_strong(expected, true))
            {
                p1.set_value();
            }
        }
    });

    // 3. Wait for the event loop to start
    std::cout << "Main thread waiting for Drogon readiness..." << std::endl;
    if (
      f1.wait_for(std::chrono::seconds(test_config::APP_STARTUP_TIMEOUT_SECONDS)) !=
      std::future_status::ready
    )
    {
        std::cerr << "TIMEOUT: drogon app failed to start within 60s!" << std::endl;
        std::_Exit(1);
        return 1;
    }
    f1.get();

    // 4. Run tests
    std::cout << "Drogon ready. Pre-warming (500ms)..." << std::endl;
    std::cout.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(test_config::APP_PREWARM_MS));

    std::cout << "Executing test::run()..." << std::endl;
    std::cout.flush();
    int status = test::run(argc, argv);
    std::cout << "test::run() completed with status: " << status << std::endl;
    std::cout.flush();

    // 5. Cleanup
    // Use _Exit() to avoid SegFault during Drogon teardown
    // Tests have already completed successfully, so we can skip potentially
    // problematic cleanup that might cause crashes in framework shutdown code
    if (status == 0)
    {
        // Clean up temporary config file before exit
        if (!runtimeConfigPath.empty() && std::filesystem::exists(runtimeConfigPath))
        {
            std::cout << "Cleaning up temporary config: " << runtimeConfigPath << std::endl;
            std::error_code ec;
            std::filesystem::remove(runtimeConfigPath, ec);
            if (ec)
            {
                std::cerr << "Warning: Failed to remove " << runtimeConfigPath << ": "
                          << ec.message() << std::endl;
            }
        }

        std::cout << "Tests passed, attempting normal teardown..." << std::endl;
        // Note: OAuth2CleanupService::stop() now uses try-catch to safely
        // handle Event loop destruction during teardown, preventing SegFault
    }
    else
    {
        std::cout << "Tests failed, attempting cleanup..." << std::endl;
    }

    // Use fast exit for successful tests to avoid Drogon framework teardown
    // issues Root cause: Triggering Event loop quit() and joining the thread
    // causes framework destruction code to run, which accesses destroyed
    // objects.
    //
    // The crash occurs INSIDE thr.join(), not after it. By exiting before
    // join(), we avoid the entire teardown process that causes the SegFault.
    //
    // This is safe because:
    // 1. Tests have completed successfully, results are recorded
    // 2. OS will clean up all resources (thread, memory, files, etc.)
    // 3. The thread will be terminated by OS when process exits
    // 4. No need for graceful framework shutdown in test environment
    if (status == 0)
    {
        std::cout << "Tests passed, using fast exit to avoid teardown SegFault..." << std::endl;
        std::_Exit(0);
    }

    // For failed tests, attempt normal teardown to get more diagnostic info
    std::cout << "Attempting graceful teardown for failed test..." << std::endl;
    try
    {
        auto loop = drogon::app().getLoop();
        if (loop && loop->isRunning())
        {
            loop->queueInLoop([=]() { drogon::app().quit(); });
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception during teardown: " << e.what() << std::endl;
    }

    if (thr.joinable())
    {
        thr.join();
    }

    return status;
}
