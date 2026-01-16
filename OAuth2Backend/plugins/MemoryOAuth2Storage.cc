#include "MemoryOAuth2Storage.h"
#include <chrono>

namespace oauth2 {

int64_t MemoryOAuth2Storage::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

void MemoryOAuth2Storage::initFromConfig(const Json::Value& clientsConfig) {
    if (clientsConfig.isNull() || !clientsConfig.isObject()) {
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const auto& clientId : clientsConfig.getMemberNames()) {
        const auto& clientData = clientsConfig[clientId];
        OAuth2Client client;
        client.clientId = clientId;
        // In memory mode, we store plain text or whatever provided as "secret"
        // Ideally we should hash it here too if we want parity, but for memory it's fine.
        client.clientSecretHash = clientData.get("secret", "").asString(); 
        
        // Handle redirect_uri (single or array)
        if (clientData["redirect_uri"].isArray()) {
            for (const auto& uri : clientData["redirect_uri"]) {
                client.redirectUris.push_back(uri.asString());
            }
        } else if (clientData["redirect_uri"].isString()) {
            client.redirectUris.push_back(clientData["redirect_uri"].asString());
        }

        clients_[clientId] = client;
    }
}

void MemoryOAuth2Storage::getClient(const std::string& clientId, ClientCallback&& cb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = clients_.find(clientId);
    if (it != clients_.end()) {
        cb(it->second);
    } else {
        cb(std::nullopt);
    }
}

void MemoryOAuth2Storage::validateClient(const std::string& clientId, 
                                         const std::string& clientSecret,
                                         BoolCallback&& cb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = clients_.find(clientId);
    if (it == clients_.end()) {
        cb(false);
        return;
    }
    
    if (clientSecret.empty()) {
        cb(true); // Public client or just ID check
        return;
    }
    
    // Simple equality check for memory storage
    bool valid = (it->second.clientSecretHash == clientSecret);
    cb(valid);
}

void MemoryOAuth2Storage::saveAuthCode(const OAuth2AuthCode& code, VoidCallback&& cb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    authCodes_[code.code] = code;
    if(cb) cb();
}

void MemoryOAuth2Storage::getAuthCode(const std::string& code, AuthCodeCallback&& cb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Clean up expired codes lazily or just check expiry
    auto it = authCodes_.find(code);
    if (it != authCodes_.end()) {
        if (it->second.expiresAt > getCurrentTimestamp()) {
            cb(it->second);
            return;
        } else {
            authCodes_.erase(it);
        }
    }
    cb(std::nullopt);
}

void MemoryOAuth2Storage::markAuthCodeUsed(const std::string& code, VoidCallback&& cb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = authCodes_.find(code);
    if (it != authCodes_.end()) {
        it->second.used = true;
    }
    if(cb) cb();
}

void MemoryOAuth2Storage::saveAccessToken(const OAuth2AccessToken& token, VoidCallback&& cb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    accessTokens_[token.token] = token;
    if(cb) cb();
}

void MemoryOAuth2Storage::getAccessToken(const std::string& token, AccessTokenCallback&& cb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = accessTokens_.find(token);
    if (it != accessTokens_.end()) {
        if (it->second.expiresAt > getCurrentTimestamp() && !it->second.revoked) {
            cb(it->second);
            return;
        }
    }
    cb(std::nullopt);
}

void MemoryOAuth2Storage::saveRefreshToken(const OAuth2RefreshToken& token, VoidCallback&& cb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    refreshTokens_[token.token] = token;
    if(cb) cb();
}

void MemoryOAuth2Storage::getRefreshToken(const std::string& token, RefreshTokenCallback&& cb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = refreshTokens_.find(token);
    if (it != refreshTokens_.end()) {
         if (it->second.expiresAt > getCurrentTimestamp() && !it->second.revoked) {
            cb(it->second);
            return;
        }
    }
    cb(std::nullopt);
}

} // namespace oauth2
