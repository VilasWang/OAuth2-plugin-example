#pragma once

#include <string>
#include <type_traits>
#include <json/json.h>
#include <oauth2/config/ConfigTypes.h>

namespace authforge::common::config
{

class ConfigManager
{
  public:
    // Load config file with environment variable overrides
    static bool load(const std::string &configPath, Json::Value &config);

    // Type-safe configuration access
    template <typename T>
    static T get(const Json::Value &config, const std::string &path, const T &defaultValue = T{});

    // Configuration validation
    static bool validate(const Json::Value &config, std::string &errorMessage);

    // Apply environment variable overrides
    static void applyEnvOverrides(Json::Value &config, const std::vector<EnvOverride> &rules);

    /**
     * @brief Get environment variable with .env file priority
     * Searches .env file first, then system environment.
     * @return value string or nullptr if not found
     */
    static const char *getEnv(const char *name);

  private:
    // Parse JSON path and return pointer to node
    static Json::Value *getJsonPointer(Json::Value &root, const std::string &path);

    // Parse integer from string
    static int parseInt(const std::string &str);
};

// Template implementations
template <typename T>
T ConfigManager::get(const Json::Value &config, const std::string &path, const T &defaultValue)
{
    Json::Value *ptr = getJsonPointer(const_cast<Json::Value &>(config), path);
    if (!ptr || ptr->isNull())
    {
        return defaultValue;
    }

    if constexpr (std::is_same_v<T, std::string>)
    {
        return ptr->asString();
    }
    else if constexpr (std::is_integral_v<T>)
    {
        return ptr->asInt();
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        return ptr->asDouble();
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
        return ptr->asBool();
    }
    else
    {
        return defaultValue;
    }
}

}  // namespace authforge::common::config
