#pragma once

#include <oauth2/storage/IOAuth2Storage.h>
#include <oauth2/utils/JwkManager.h>
#include <drogon/drogon.h>
#include <memory>
#include <string>
#include <functional>

namespace oauth2
{

// M2b Task 17 slice 10: oauth2::JwkManager is now a type alias (see
// OAuth2Plugin/include/oauth2/utils/JwkManager.h's compatibility shim),
// not a class -- a forward `class JwkManager;` declaration here would
// conflict with that alias. Include the (lightweight) shim header
// instead; JwkManager.h no longer pulls in anything Drogon-heavy so this
// does not meaningfully change TokenService.h's include weight.

// Defect 1.9 fix (async-chain dangling `this`): TokenService inherits
// std::enable_shared_from_this so its asynchronous storage chains
// (exchangeCodeForToken / refreshAccessToken) can capture `auto self =
// shared_from_this();` at the outermost async call and thread that same `self`
// through every nested continuation, keeping the service alive until the
// in-flight callback completes (no use-after-free on teardown). The service is
// always created via std::make_shared (OAuth2Plugin::initAndStart), so
// shared_from_this() is valid at runtime. The synchronous pure-function call
// sites (validatePkceCodeVerifier / generateSha256Hash via a stack-constructed
// TokenService(nullptr) temporary) never call shared_from_this(), so they keep
// working without shared ownership.
class TokenService : public std::enable_shared_from_this<TokenService>
{
  public:
    // Shared ownership of the storage (defect 1.3 fix): the service holds a
    // std::shared_ptr<IOAuth2Storage> instead of a raw IOAuth2Storage*, so the
    // storage lifetime is guaranteed to cover every user. A null shared_ptr is
    // accepted for the pure-function call sites (e.g.
    // OAuth2Plugin::validatePkceCodeVerifier via TokenService(nullptr)).
    explicit TokenService(
      std::shared_ptr<IOAuth2Storage> storage,
      int64_t authCodeTtl = 600,
      int64_t accessTokenTtl = 3600,
      int64_t refreshTokenTtl = 2592000
    );

    // Defect 1.5 fix (immutable publish): the JwkManager is published as a
    // std::shared_ptr<const JwkManager>. OAuth2Plugin constructs it, calls
    // init() exactly once during startup, then hands it here as a const
    // pointer — so this service can only READ it (signJwt/getJwks/getKeyId,
    // all const), and the type system forbids any run-time mutation.
    void setJwkManager(std::shared_ptr<const JwkManager> jwkManager)
    {
        jwkManager_ = std::move(jwkManager);
    }

    /**
     * @brief Generate Authorization Code
     */
    void generateAuthorizationCode(
      const std::string &clientId,
      const std::string &subject,
      const std::string &scope,
      const std::string &redirectUri,
      const std::string &codeChallenge,
      const std::string &codeChallengeMethod,
      const std::string &nonce,
      std::function<void(bool, std::string, std::string)> &&callback
    );

    /**
     * @brief Exchange Code for Access Token
     */
    void exchangeCodeForToken(
      const std::string &code,
      const std::string &clientId,
      const std::string &clientSecret,
      const std::string &redirectUri,
      const std::string &codeVerifier,
      std::function<void(const Json::Value &)> &&callback
    );

    /**
     * @brief Refresh Access Token
     */
    void refreshAccessToken(
      const std::string &refreshToken,
      const std::string &clientId,
      std::function<void(const Json::Value &)> &&callback
    );

    /**
     * @brief Validate Access Token
     */
    void validateAccessToken(
      const std::string &token,
      std::function<void(std::shared_ptr<OAuth2AccessToken>)> &&callback
    );

    /**
     * @brief Introspect Token (RFC 7662)
     */
    void introspectToken(
      const std::string &token,
      std::function<void(std::optional<TokenIntrospection>)> &&callback
    );

    /**
     * @brief Revoke Access Token (RFC 7009)
     */
    void revokeAccessToken(
      const std::string &token,
      const std::string &revokedBy,
      std::function<void()> &&callback
    );

    bool validatePkceCodeVerifier(
      const std::string &codeVerifier,
      const std::string &codeChallenge,
      const std::string &codeChallengeMethod
    );

    std::string generateSha256Hash(const std::string &input);

  private:
    std::shared_ptr<IOAuth2Storage> storage_;
    int64_t authCodeTtl_;
    int64_t accessTokenTtl_;
    int64_t refreshTokenTtl_;
    std::shared_ptr<const JwkManager> jwkManager_;
};

}  // namespace oauth2
