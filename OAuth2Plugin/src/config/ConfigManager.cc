#include <oauth2/config/ConfigManager.h>
#include <drogon/drogon.h>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <filesystem>

namespace common::config
{

// .env file contents (loaded once) — wrapped in function to guarantee
// construction-on-first-use (avoids static init order fiasco, see P5 bugfix).
static std::unordered_map<std::string, std::string> &getDotEnvVars()
{
    static std::unordered_map<std::string, std::string> instance;
    return instance;
}

static bool dotEnvLoaded_ = false;

/**
 * @brief Load .env file into memory
 * Searches for .env in current directory and parent directories.
 * Format: KEY=VALUE (one per line, # comments, empty lines ignored)
 */
static void loadDotEnv()
{
    if (dotEnvLoaded_)
        return;
    dotEnvLoaded_ = true;

    // Search paths for .env file
    std::vector<std::string> searchPaths = {
      ".env",
      "../.env",
      "../../.env",
    };

    std::string envPath;
    for (const auto &path : searchPaths)
    {
        if (std::filesystem::exists(path))
        {
            envPath = path;
            break;
        }
    }

    if (envPath.empty())
        return;

    std::ifstream file(envPath);
    if (!file.is_open())
        return;

    LOG_INFO << "Loading .env file: " << std::filesystem::absolute(envPath).string();

    std::string line;
    while (std::getline(file, line))
    {
        // Trim whitespace
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
            line.erase(line.begin());
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
            line.pop_back();

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#')
            continue;

        // Parse KEY=VALUE
        auto eqPos = line.find('=');
        if (eqPos == std::string::npos)
            continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

        // Trim key
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
            key.pop_back();

        // Trim value (remove surrounding quotes if present)
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.erase(value.begin());
        if (
          value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                (value.front() == '\'' && value.back() == '\''))
        )
        {
            value = value.substr(1, value.size() - 2);
        }

        if (!key.empty())
        {
            getDotEnvVars()[key] = value;
        }
    }

    if (!getDotEnvVars().empty())
    {
        LOG_INFO << "Loaded " << getDotEnvVars().size() << " variables from .env file";
    }
}

/**
 * @brief Get environment variable value
 * Priority: .env file > system environment variable
 */
static const char *getEnvValue(const char *name)
{
    // Priority 1: .env file
    auto it = getDotEnvVars().find(name);
    if (it != getDotEnvVars().end() && !it->second.empty())
    {
        return it->second.c_str();
    }

    // Priority 2: system environment variable
    return std::getenv(name);
}

bool ConfigManager::load(const std::string &configPath, Json::Value &config)
{
    std::ifstream configFile(configPath);
    if (!configFile.is_open())
    {
        LOG_ERROR << "Config file not found: " << configPath;
        return false;
    }

    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, configFile, &config, &errs))
    {
        LOG_ERROR << "Failed to parse config file: " << errs;
        return false;
    }

    // Load .env file first (before applying overrides)
    loadDotEnv();

    // Apply environment variable overrides (.env takes priority over system env)
    applyEnvOverrides(config, OAUTH2_ENV_OVERRIDES);

    return true;
}

void ConfigManager::applyEnvOverrides(Json::Value &config, const std::vector<EnvOverride> &rules)
{
    for (const auto &rule : rules)
    {
        const char *envValue = getEnvValue(rule.envVar);
        if (envValue)
        {
            Json::Value *ptr = getJsonPointer(config, rule.configPath);
            if (ptr)
            {
                if (rule.isNumeric)
                {
                    *ptr = parseInt(envValue);
                }
                else if (rule.isStringList)
                {
                    // Split comma-separated string into a JSON array of trimmed strings.
                    Json::Value arrayValue(Json::arrayValue);
                    std::stringstream ss(envValue);
                    std::string token;
                    while (std::getline(ss, token, ','))
                    {
                        auto start = token.find_first_not_of(" \t");
                        auto end = token.find_last_not_of(" \t");
                        if (start != std::string::npos)
                        {
                            arrayValue.append(token.substr(start, end - start + 1));
                        }
                    }
                    *ptr = arrayValue;
                }
                else
                {
                    *ptr = envValue;
                }
            }
        }
    }
}

Json::Value *ConfigManager::getJsonPointer(Json::Value &root, const std::string &path)
{
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string part;

    while (std::getline(ss, part, '.'))
    {
        parts.push_back(part);
    }

    Json::Value *current = &root;
    for (const auto &p : parts)
    {
        if (current->isNull())
        {
            return nullptr;
        }

        // Check if it's an array index
        if (!p.empty() && std::all_of(p.begin(), p.end(), ::isdigit))
        {
            if (!current->isArray())
            {
                return nullptr;
            }
            size_t index = std::stoul(p);
            if (index >= current->size())
            {
                return nullptr;
            }
            current = &((*current)[static_cast<int>(index)]);
        }
        // Named-element lookup in an object array. Supports two spellings:
        //   "[name=OAuth2Plugin]"            — filter the current array
        //   "plugins[name=OAuth2Plugin]"     — first access member "plugins",
        //                                       then filter the resulting array.
        // Decouples override paths from plugin ordering, which differs per config
        // file (Hodor/AccessLogger are inserted in some configs but not others).
        //
        // LIMITATION: the path is split on '.' before this branch runs, so the
        // filter value must not contain '.'. Current plugin names (OAuth2Plugin,
        // Hodor, PromExporter) are safe; a future namespaced name like
        // "drogon.plugin.Foo" would need the tokenizer to ignore '.' inside [].
        else if (p.size() > 2 && p.back() == ']')
        {
            std::string member;
            std::string body = p;
            auto lb = body.find('[');
            if (lb == std::string::npos)
            {
                return nullptr;
            }
            member = body.substr(0, lb);
            body = body.substr(lb + 1, body.size() - lb - 2);  // strip [ and ]

            auto eq = body.find('=');
            if (eq == std::string::npos)
            {
                return nullptr;
            }
            std::string key = body.substr(0, eq);
            std::string val = body.substr(eq + 1);

            Json::Value *arr = current;
            if (!member.empty())
            {
                if (!current->isObject() || !current->isMember(member))
                {
                    return nullptr;
                }
                arr = &((*current)[member]);
            }
            if (!arr->isArray())
            {
                return nullptr;
            }
            bool found = false;
            for (auto &elem : *arr)
            {
                if (
                  elem.isObject() && elem.isMember(key) && elem[key].isString() &&
                  elem[key].asString() == val
                )
                {
                    current = &elem;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return nullptr;
            }
        }
        else
        {
            if (!current->isObject() || !current->isMember(p))
            {
                return nullptr;
            }
            current = &((*current)[p]);
        }
    }

    return current;
}

int ConfigManager::parseInt(const std::string &str)
{
    try
    {
        return std::stoi(str);
    }
    catch (...)
    {
        return 0;
    }
}

bool ConfigManager::validate(const Json::Value &config, std::string &errorMessage)
{
    // Check db_clients section exists and is an array
    if (!config.isMember("db_clients") || !config["db_clients"].isArray())
    {
        errorMessage = "Missing or invalid 'db_clients' configuration";
        return false;
    }

    // Check redis_clients section exists and is an array
    if (!config.isMember("redis_clients") || !config["redis_clients"].isArray())
    {
        errorMessage = "Missing or invalid 'redis_clients' configuration";
        return false;
    }

    // Validate port ranges if db_clients is not empty
    if (config["db_clients"].size() > 0 && config["db_clients"][0].isMember("port"))
    {
        int port = config["db_clients"][0]["port"].asInt();
        if (port < 1 || port > 65535)
        {
            errorMessage = "Database port out of range (1-65535)";
            return false;
        }
    }

    // Validate port ranges if redis_clients is not empty
    if (config["redis_clients"].size() > 0 && config["redis_clients"][0].isMember("port"))
    {
        int port = config["redis_clients"][0]["port"].asInt();
        if (port < 1 || port > 65535)
        {
            errorMessage = "Redis port out of range (1-65535)";
            return false;
        }
    }

    // Production-mode validation
    const char *env = getEnvValue("OAUTH2_ENV");
    bool isProd = (env && std::string(env) == "production");

    if (isProd)
    {
        // Issuer must be HTTPS in production
        std::string issuer;
        if (
          config.isMember("custom_config") && config["custom_config"].isMember("metadata") &&
          config["custom_config"]["metadata"].isMember("issuer")
        )
        {
            issuer = config["custom_config"]["metadata"]["issuer"].asString();
        }
        if (issuer.empty() || issuer.find("https://") != 0)
        {
            errorMessage =
              "Production requires HTTPS issuer (set custom_config.metadata.issuer "
              "or OAUTH2_ISSUER env var to https://...)";
            return false;
        }

        // DB password must not be default
        if (config["db_clients"].size() > 0)
        {
            std::string dbPass = config["db_clients"][0].get("passwd", "").asString();
            if (dbPass.empty() || dbPass == "123456" || dbPass == "password")
            {
                errorMessage =
                  "Production requires non-default database password "
                  "(set OAUTH2_DB_PASSWORD env var)";
                return false;
            }
        }

        // Redis password must not be default
        if (config["redis_clients"].size() > 0)
        {
            std::string redisPass = config["redis_clients"][0].get("passwd", "").asString();
            if (redisPass == "123456" || redisPass == "password")
            {
                errorMessage =
                  "Production requires non-default Redis password "
                  "(set OAUTH2_REDIS_PASSWORD env var)";
                return false;
            }
        }
    }

    return true;
}

const char *ConfigManager::getEnv(const char *name)
{
    loadDotEnv();
    return getEnvValue(name);
}

}  // namespace common::config
