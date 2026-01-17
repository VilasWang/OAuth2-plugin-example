#pragma once

#include <drogon/plugins/Plugin.h>
#include "IOAuth2Storage.h"
#include <string>
#include <memory>
#include <functional>

class OAuth2Plugin : public drogon::Plugin<OAuth2Plugin>
{
public:
    using AccessToken = oauth2::OAuth2AccessToken;
    using Client = oauth2::OAuth2Client;

    OAuth2Plugin() = default;
    void initAndStart(const Json::Value &config) override;
    void shutdown() override;

    // ========== Async API with Callbacks ==========
    
    /**
     * @brief Validate if client exists and secret matches (Async)
     */
    void validateClient(const std::string &clientId, 
                        const std::string &clientSecret, 
                        std::function<void(bool)>&& callback);
    
    /**
     * @brief Validate redirect URI (Async)
     */
    void validateRedirectUri(const std::string &clientId, 
                             const std::string &redirectUri,
                             std::function<void(bool)>&& callback);

    /**
     * @brief Generate Authorization Code (Async)
     */
    void generateAuthorizationCode(const std::string &clientId, 
                                   const std::string &userId, 
                                   const std::string &scope,
                                   std::function<void(std::string)>&& callback);

    /**
     * @brief Exchange Code for Access Token (Async)
     * Returns JSON with {access_token, refresh_token, expires_in} or {error}
     */
    void exchangeCodeForToken(const std::string &code, 
                              const std::string &clientId,
                              std::function<void(const Json::Value&)>&& callback);

    /**
     * @brief Refresh Access Token (Async)
     * Returns JSON with {access_token, refresh_token, expires_in} or {error}
     */
    void refreshAccessToken(const std::string &refreshToken,
                            const std::string &clientId,
                            std::function<void(const Json::Value&)>&& callback);

    /**
     * @brief Validate Access Token (Async)
     */
    void validateAccessToken(const std::string &token,
                             std::function<void(std::shared_ptr<AccessToken>)>&& callback);

    // ========== Storage Access ==========
    oauth2::IOAuth2Storage* getStorage() { return storage_.get(); }

private:
    std::unique_ptr<oauth2::IOAuth2Storage> storage_;
    std::string storageType_;
    void initStorage(const Json::Value &config);
};
