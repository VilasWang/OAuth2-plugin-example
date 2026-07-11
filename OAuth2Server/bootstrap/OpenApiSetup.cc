#include "OpenApiSetup.h"
#include <drogon/drogon.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <filesystem>
#include <string>

namespace bootstrap
{

void setupOpenApi()
{
    // Configure OpenAPI server from config
    const auto &listeners = drogon::app().getListeners();
    const auto &customConfig = drogon::app().getCustomConfig();
    if (
      !listeners.empty() && customConfig.isMember("listeners") &&
      customConfig["listeners"].isArray() && !customConfig["listeners"].empty()
    )
    {
        const auto &listener = listeners[0];
        const auto &listenerConfig = customConfig["listeners"][0];

        std::string host = listener.toIp();
        uint16_t port = listener.toPort();
        bool isHttps = listenerConfig.get("https", false).asBool();

        // Build server URL
        std::string serverUrl;
        if (isHttps)
        {
            serverUrl = "https://" + host;
        }
        else
        {
            serverUrl = "http://" + host;
        }

        // Add port if not default
        if ((isHttps && port != 443) || (!isHttps && port != 80))
        {
            serverUrl += ":" + std::to_string(port);
        }

        authforge::drogon::observability::openapi::OpenApiGenerator::setServerConfig(
          serverUrl, "OAuth2 Authorization Server"
        );
    }

    // Use current working directory (usually build/Release or project root)
    std::filesystem::path baseDir = std::filesystem::current_path();
    std::string openapiPath = (baseDir / "docs" / "api" / "openapi.json").string();

    if (!authforge::drogon::observability::openapi::OpenApiGenerator::writeToFile(openapiPath))
    {
        LOG_WARN << "Failed to write OpenAPI specification";
    }
    else
    {
        LOG_INFO << "OpenAPI specification generated: " << openapiPath;
    }
}

}  // namespace bootstrap
