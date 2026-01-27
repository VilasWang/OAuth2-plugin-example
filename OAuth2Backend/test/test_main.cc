#define DROGON_TEST_MAIN
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <filesystem> // Ensure this is included

// Helper to create log directory from config
void createLogDirFromConfig(const std::string &configPath)
{
    std::ifstream configFile(configPath);
    if (!configFile.is_open())
        return;

    Json::Value root;
    Json::Reader reader;
    if (reader.parse(configFile, root))
    {
        const auto &logConfig = root["app"]["log"];
        if (!logConfig.isNull())
        {
            std::string logPath = logConfig.get("log_path", "").asString();
            if (!logPath.empty())
            {
                // Handle relative paths in tests (relative to build dir usually, or CWD)
                try
                {
                   std::filesystem::path path(logPath);
                   // Verify if we need to resolve it relative to config file location or CWD
                   // For simplicity, we assume CWD or relative to it, just like drogon does for the most part
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

DROGON_TEST(BasicTest)
{
    // Add your tests here
}

int main(int argc, char **argv)
{
    using namespace drogon;

    std::promise<void> p1;
    std::future<void> f1 = p1.get_future();

    // Start the main loop on another thread
    std::thread thr([&]() {
        // Load Config for Integration Tests BEFORE app().run()
        std::string configPath = "./config.json";
        if (!std::filesystem::exists(configPath))
            configPath = "../config.json";
        if (!std::filesystem::exists(configPath))
            configPath = "../../../config.json";

        if (std::filesystem::exists(configPath))
        {
            std::cout << "Loading config from: " << configPath << std::endl;
            createLogDirFromConfig(configPath);
            app().loadConfigFile(configPath);
        }
        else
        {
            std::cerr << "WARNING: config.json not found. Integration tests "
                         "might fail."
                      << std::endl;
        }

        // Use registerBeginningAdvice to signal that the app is ready
        // This fires AFTER all plugins and DB connections are initialized
        app().registerBeginningAdvice([&p1]() {
            std::cout << "Drogon app ready, signaling tests to start..."
                      << std::endl;
            p1.set_value();
        });

        app().run();
    });

    // The future is only satisfied after the event loop started
    f1.get();
    int status = test::run(argc, argv);

    // Ask the event loop to shutdown and wait
    app().getLoop()->queueInLoop([]() { app().quit(); });
    thr.join();
    return status;
}
