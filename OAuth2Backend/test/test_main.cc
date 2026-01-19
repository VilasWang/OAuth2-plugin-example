#define DROGON_TEST_MAIN
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <filesystem>
#include <iostream>

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
        std::string configPath = "../../config.json";
        if (!std::filesystem::exists(configPath))
            configPath = "../config.json";
        if (!std::filesystem::exists(configPath))
            configPath = "../../../config.json";

        if (std::filesystem::exists(configPath))
        {
            std::cout << "Loading config from: " << configPath << std::endl;
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
            std::cout << "Drogon app ready, signaling tests to start..." << std::endl;
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
