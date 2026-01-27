#include <drogon/drogon.h>
#include <vector>
#include <string>
#include <algorithm>

using namespace drogon;

#include <filesystem>
#include <fstream>
#include <iostream>

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
                try
                {
                    if (!std::filesystem::exists(logPath))
                    {
                        std::filesystem::create_directories(logPath);
                        std::cout << "Created log directory: " << logPath << std::endl;
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

void setupCors()
{
    // Define the whitelist check logic
    auto isAllowed = [](const std::string &origin) -> bool {
        if (origin.empty())
            return false;

        const auto &customConfig = drogon::app().getCustomConfig();
        const auto &allowOrigins = customConfig["cors"]["allow_origins"];

        if (allowOrigins.isArray())
        {
            for (const auto &allowed : allowOrigins)
            {
                auto allowedStr = allowed.asString();
                if (allowedStr == "*" || allowedStr == origin)
                    return true;
            }
        }
        return false;
    };

    // Register sync advice to handle CORS preflight (OPTIONS) requests
    drogon::app().registerSyncAdvice(
        [isAllowed](
            const drogon::HttpRequestPtr &req) -> drogon::HttpResponsePtr {
            if (req->method() == drogon::HttpMethod::Options)
            {
                const auto &origin = req->getHeader("Origin");
                if (isAllowed(origin))
                {
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->addHeader("Access-Control-Allow-Origin", origin);

                    const auto &requestMethod =
                        req->getHeader("Access-Control-Request-Method");
                    if (!requestMethod.empty())
                    {
                        resp->addHeader("Access-Control-Allow-Methods",
                                        requestMethod);
                    }

                    resp->addHeader("Access-Control-Allow-Credentials", "true");

                    const auto &requestHeaders =
                        req->getHeader("Access-Control-Request-Headers");
                    if (!requestHeaders.empty())
                    {
                        resp->addHeader("Access-Control-Allow-Headers",
                                        requestHeaders);
                    }
                    return resp;
                }
            }
            return {};
        });

    // Register post-handling advice to add CORS headers to all responses
    drogon::app().registerPostHandlingAdvice(
        [isAllowed](const drogon::HttpRequestPtr &req,
                    const drogon::HttpResponsePtr &resp) {
            const auto &origin = req->getHeader("Origin");
            if (isAllowed(origin))
            {
                resp->addHeader("Access-Control-Allow-Origin", origin);
                resp->addHeader("Access-Control-Allow-Methods",
                                "GET, POST, OPTIONS");
                resp->addHeader("Access-Control-Allow-Headers",
                                "Content-Type, Authorization");
                resp->addHeader("Access-Control-Allow-Credentials", "true");
            }
        });
}

int main()
{
    // Set HTTP listener address and port
    // drogon::app().addListener("0.0.0.0", 5555);

    // Load config file - important to do this BEFORE setupCors if setupCors
    // uses getCustomConfig() Actually, getCustomConfig() is populated after
    // loadConfigFile()
    // Load config file - important to do this BEFORE setupCors if setupCors
    // uses getCustomConfig() Actually, getCustomConfig() is populated after
    // loadConfigFile()
    
    // Ensure log directory exists
    createLogDirFromConfig("./config.json");

    drogon::app().loadConfigFile("./config.json");

    // Setup CORS support
    setupCors();

    // Global Security Headers
    drogon::app().registerPostHandlingAdvice(
        [](const drogon::HttpRequestPtr &,
           const drogon::HttpResponsePtr &resp) {
            resp->addHeader("X-Content-Type-Options", "nosniff");
            resp->addHeader("X-Frame-Options", "SAMEORIGIN");
            resp->addHeader("Content-Security-Policy",
                            "default-src 'self'; frame-ancestors 'self';");
            resp->addHeader("Strict-Transport-Security",
                            "max-age=31536000; includeSubDomains");
        });

    drogon::app().run();
    return 0;
}
