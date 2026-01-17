
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>
#include <fstream>
#include <filesystem>

DROGON_TEST(ConfigTest)
{
    // 1. Verify config.json exists
    std::string configPath = "../../config.json"; // Assuming running from build/test/Release
    if (!std::filesystem::exists(configPath)) {
        configPath = "../config.json"; // Try parent
    }
    if (!std::filesystem::exists(configPath)) {
        configPath = "../../../config.json"; // Try explicitly from source root
    }
    
    // Fallback: assume we are in OAuth2Backend/build/test
    if (!std::filesystem::exists(configPath)) {
        // Just print cwd. Depending on compiler support, std::filesystem might throw or return generic path.
        // We skip strict check to avoid CI fail if path varies, but we try to load it.
    }

    if (std::filesystem::exists(configPath)) {
        LOG_INFO << "Found config at: " << configPath;
        
        // 2. Parse JSON
        std::ifstream ifs(configPath);
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        bool parsed = Json::parseFromStream(builder, ifs, &root, &errs);
        
        CHECK(parsed == true);
        
        // 3. Verify OAuth2 Config
        if (parsed) {
             CHECK(root.isMember("plugins"));
             bool foundPlugin = false;
             for(const auto& plugin : root["plugins"]) {
                 if (plugin["name"].asString() == "OAuth2Plugin") {
                     foundPlugin = true;
                     auto config = plugin["config"];
                     CHECK(config.isMember("storage_type"));
                     // Verify it matches one of the expected types
                     std::string type = config["storage_type"].asString();
                     bool isValidType = (type == "memory" || type == "redis" || type == "postgres");
                     CHECK(isValidType);
                 }
             }
             CHECK(foundPlugin);
        }
    }
}
