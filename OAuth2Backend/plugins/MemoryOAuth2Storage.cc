#include "MemoryOAuth2Storage.h"
#include <drogon/drogon.h>
#include <chrono>
#include <algorithm>

namespace oauth2 {

void MemoryOAuth2Storage::initFromConfig(const Json::Value& clientsConfig)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!clientsConfig.isObject()) {
        return;
    }
    
    for (auto it = clientsConfig.begin(); it != clientsConfig.end(); ++it)
    {
        OAuth2Client client;
        client.clientId = it.key().asString();
        client.clientSecretHash = (*it)["secret"].asString();
        
        // Support single or multiple redirect URIs
        if ((*it)["redirect_uri"].isString()) {
            client.redirectUris.push_back((*it)["redirect_uri"].asString());
        } else if ((*it)["redirect_uris"].isArray()) {
            for (const auto& uri : (*it)["redirect_uris"]) {
                client.redirectUris.push_back(uri.asString());
            }
        }
        
        // Parse allowed scopes
        if ((*it)["scopes"].isArray()) {
            for (const auto& scope : (*it)["scopes"]) {
                client.allowedScopes.push_back(scope.asString());
            }
        }
        
        clients_[client.clientId] = client;
        LOG_INFO << "MemoryStorage: Loaded client: " << client.clientId;
    }
}

std::optional<OAuth2Client> MemoryOAuth2Storage::getClient(const std::string& clientId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clients_.find(clientId);
    if (it != clients_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool MemoryOAuth2Storage::validateClient(const std::string& clientId, 
                                         const std::string& clientSecret)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clients_.find(clientId);
    if (it == clients_.end()) {
        return false;
    }
    
    // If no secret provided, just check existence
    if (clientSecret.empty()) {
        return true;
    }
    
    // For memory storage, we do plain comparison
    // In production with database, this should use bcrypt_verify
    return it->second.clientSecretHash == clientSecret;
}

void MemoryOAuth2Storage::saveAuthCode(const OAuth2AuthCode& code)
{
    std::lock_guard<std::mutex> lock(mutex_);
    authCodes_[code.code] = code;
}

std::optional<OAuth2AuthCode> MemoryOAuth2Storage::getAuthCode(const std::string& code)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = authCodes_.find(code);
    if (it == authCodes_.end()) {
        return std::nullopt;
    }
    
    // Check if expired or used
    if (it->second.used || getCurrentTimestamp() > it->second.expiresAt) {
        return std::nullopt;
    }
    
    return it->second;
}

void MemoryOAuth2Storage::markAuthCodeUsed(const std::string& code)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = authCodes_.find(code);
    if (it != authCodes_.end()) {
        it->second.used = true;
    }
}

void MemoryOAuth2Storage::cleanupExpiredAuthCodes()
{
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t now = getCurrentTimestamp();
    
    for (auto it = authCodes_.begin(); it != authCodes_.end(); ) {
        if (it->second.expiresAt < now || it->second.used) {
            it = authCodes_.erase(it);
        } else {
            ++it;
        }
    }
}

void MemoryOAuth2Storage::saveAccessToken(const OAuth2AccessToken& token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    accessTokens_[token.token] = token;
}

std::optional<OAuth2AccessToken> MemoryOAuth2Storage::getAccessToken(const std::string& token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = accessTokens_.find(token);
    if (it == accessTokens_.end()) {
        return std::nullopt;
    }
    
    // Check if revoked or expired
    if (it->second.revoked || getCurrentTimestamp() > it->second.expiresAt) {
        return std::nullopt;
    }
    
    return it->second;
}

void MemoryOAuth2Storage::revokeAccessToken(const std::string& token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = accessTokens_.find(token);
    if (it != accessTokens_.end()) {
        it->second.revoked = true;
    }
}

void MemoryOAuth2Storage::revokeAllUserTokens(const std::string& userId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [key, token] : accessTokens_) {
        if (token.userId == userId) {
            token.revoked = true;
        }
    }
    for (auto& [key, token] : refreshTokens_) {
        if (token.userId == userId) {
            token.revoked = true;
        }
    }
}

void MemoryOAuth2Storage::saveRefreshToken(const OAuth2RefreshToken& token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    refreshTokens_[token.token] = token;
}

std::optional<OAuth2RefreshToken> MemoryOAuth2Storage::getRefreshToken(const std::string& token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = refreshTokens_.find(token);
    if (it == refreshTokens_.end()) {
        return std::nullopt;
    }
    
    if (it->second.revoked || getCurrentTimestamp() > it->second.expiresAt) {
        return std::nullopt;
    }
    
    return it->second;
}

void MemoryOAuth2Storage::revokeRefreshToken(const std::string& token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = refreshTokens_.find(token);
    if (it != refreshTokens_.end()) {
        it->second.revoked = true;
    }
}

int64_t MemoryOAuth2Storage::getCurrentTimestamp() const
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace oauth2
