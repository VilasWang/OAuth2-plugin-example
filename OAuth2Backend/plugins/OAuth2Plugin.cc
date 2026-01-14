#include "OAuth2Plugin.h"
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <random>

using namespace drogon;

void OAuth2Plugin::initAndStart(const Json::Value &config)
{
    LOG_INFO << "OAuth2Plugin loading...";
    auto clients = config["clients"];
    if (clients.isObject())
    {
        for (auto it = clients.begin(); it != clients.end(); ++it)
        {
            Client c;
            c.clientId = it.key().asString();
            c.clientSecret = (*it)["secret"].asString();
            c.redirectUri = (*it)["redirect_uri"].asString();
            clients_[c.clientId] = c;
            LOG_INFO << "Loaded client: " << c.clientId;
        }
    }
}

void OAuth2Plugin::shutdown()
{
    LOG_INFO << "OAuth2Plugin shutdown";
}

bool OAuth2Plugin::validateClient(const std::string &clientId, const std::string &clientSecret)
{
    auto it = clients_.find(clientId);
    if (it == clients_.end())
        return false;
    
    if (!clientSecret.empty() && it->second.clientSecret != clientSecret)
        return false;

    return true;
}

bool OAuth2Plugin::validateRedirectUri(const std::string &clientId, const std::string &redirectUri)
{
    auto it = clients_.find(clientId);
    if (it == clients_.end())
        return false;
    // For simplicity, strict match. In real world, allows subdirectory or multiple URIs.
    return it->second.redirectUri == redirectUri;
}

std::string OAuth2Plugin::generateAuthorizationCode(const std::string &clientId, const std::string &userId, const std::string &scope)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto code = utils::getUuid(); 
    AuthCode ac;
    ac.code = code;
    ac.clientId = clientId;
    ac.userId = userId;
    ac.scope = scope;
    ac.timestamp = trantor::Date::now().microSecondsSinceEpoch();
    authCodes_[code] = ac;
    return code;
}

std::string OAuth2Plugin::exchangeCodeForToken(const std::string &code, const std::string &clientId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = authCodes_.find(code);
    if (it == authCodes_.end())
    {
        LOG_WARN << "Invalid code: " << code;
        return "";
    }

    if (it->second.clientId != clientId)
    {
        LOG_WARN << "Client ID mismatch for code";
        return "";
    }

    // Check expiration (e.g., 10 minutes)
    long long now = trantor::Date::now().microSecondsSinceEpoch();
    if (now - it->second.timestamp > 10 * 60 * 1000000)
    {
        LOG_WARN << "Code expired";
        authCodes_.erase(it);
        return "";
    }

    // Generate Token
    auto token = utils::getUuid();
    auto at = std::make_shared<AccessToken>();
    at->token = token;
    at->clientId = clientId;
    at->userId = it->second.userId;
    at->scope = it->second.scope;
    at->timestamp = now;
    at->expires = 3600; // 1 hour

    accessTokens_[token] = at;
    
    // Remove used code
    authCodes_.erase(it);

    return token;
}

std::shared_ptr<OAuth2Plugin::AccessToken> OAuth2Plugin::validateAccessToken(const std::string &token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = accessTokens_.find(token);
    if (it == accessTokens_.end())
        return nullptr;
    
    // Check expiry
    long long now = trantor::Date::now().microSecondsSinceEpoch();
    // timestamp is microseconds, expires is seconds.
    if ((now - it->second->timestamp) / 1000000 > it->second->expires)
    {
        return nullptr;
    }

    return it->second;
}

std::shared_ptr<OAuth2Plugin::Client> OAuth2Plugin::getClient(const std::string &clientId)
{
    auto it = clients_.find(clientId);
    if (it != clients_.end())
        return std::make_shared<Client>(it->second);
    return nullptr;
}
