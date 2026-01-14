#pragma once

#include <drogon/plugins/Plugin.h>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <mutex>

class OAuth2Plugin : public drogon::Plugin<OAuth2Plugin>
{
public:
    struct Client
    {
        std::string clientId;
        std::string clientSecret;
        std::string redirectUri;
    };

    struct AuthCode
    {
        std::string code;
        std::string clientId;
        std::string userId;
        std::string scope;
        long long timestamp;
    };

    struct AccessToken
    {
        std::string token;
        std::string clientId;
        std::string userId;
        std::string scope;
        long long timestamp;
        long long expires;
    };

    OAuth2Plugin() {}
    void initAndStart(const Json::Value &config) override;
    void shutdown() override;

    // Validate if client exists and secret matches (if provided)
    bool validateClient(const std::string &clientId, const std::string &clientSecret = "");
    bool validateRedirectUri(const std::string &clientId, const std::string &redirectUri);

    // Generate and store Authorization Code
    std::string generateAuthorizationCode(const std::string &clientId, const std::string &userId, const std::string &scope);

    // Exchange Code for Access Token
    // Returns empty string if invalid
    std::string exchangeCodeForToken(const std::string &code, const std::string &clientId);

    // Validate Access Token
    // Returns pointer to token info or null
    std::shared_ptr<AccessToken> validateAccessToken(const std::string &token);

    // Get Client info
    std::shared_ptr<Client> getClient(const std::string &clientId);

private:
    std::map<std::string, Client> clients_;
    
    // In-memory storage for demo purposes
    std::map<std::string, AuthCode> authCodes_; // code -> AuthCode
    std::map<std::string, std::shared_ptr<AccessToken>> accessTokens_; // token -> AccessToken
    
    std::mutex mutex_;
};
