#pragma once

// Task 17 remainder (authforge-sdk-refactor, design.md §6/§8 "protocol/"):
// Domain-layer TokenService, ported from
// OAuth2Plugin/include/oauth2/services/TokenService.h onto the new split
// repository interfaces (authforge::oauth2::repository::IClientRepository
// / IGrantRepository / ITokenRepository, Task 17 slice 3) instead of the
// old god interface oauth2::IOAuth2Storage, and onto Domain-layer ports
// (ICryptoProvider/IAuditSink/ISubjectResolver/IRoleProvider, all from
// libs/common) instead of OAuth2Plugin's Adapter-side concrete classes
// (OpenSslCryptoProvider/AuditLogger) and drogon::LOG_* macros.
//
// Behavioral parity with the original is the goal for every method this
// class implements -- same JSON response shapes, same error codes/
// messages, same TTL/expiry semantics, same refresh-token-reuse cascade
// revocation, same at-rest token hashing (protocol::hashToken, UPPERCASE
// hex -- see TokenCrypto.h for why the case must not change). The ONE
// deliberate behavior difference: PKCE S256 verification here delegates
// to oauth2::pkce::verifyCodeVerifier (the RFC 7636 §4.6-conformant
// implementation, Task 17 slice 1) rather than duplicating
// TokenService.cc's own validatePkceCodeVerifier/generateSha256Hash pair
// -- the original OAuth2Plugin-side class already made this same fix in
// Task 17 slice 4, so this port simply carries the ALREADY-FIXED
// behavior forward rather than reintroducing the pre-fix defect.
//
// "roles" resolution: the original storage_->getUserRoles(subject, ...)
// call is a single-repository lookup keyed by the OAuth2 subject string.
// The split repository interfaces do not carry a getUserRoles method at
// all (design.md's aggregate split places user/role lookups with
// identity, not oauth2) -- so this port resolves roles via the two
// Domain ports design.md's §5.2/§5.3 sections define for exactly this
// cross-package need: ISubjectResolver (subject -> internalUserId) then
// IRoleProvider (internalUserId -> roles). Both are optional
// (nullable/constructor-defaulted): if either is not wired, the "roles"
// field in the token response is simply empty, which is a safe default
// (roles are informational metadata in the response, not used for access
// control decisions inside this class).
//
// NOT YET WIRED INTO PRODUCTION: OAuth2Plugin continues to use its own
// oauth2::TokenService against the old IOAuth2Storage. Wiring this class
// into apps/server's assembly is Task 24, deferred until libs/identity's
// remaining services (MFA/WebAuthn/Social/Session) are filled in (see
// PROGRESS.md). This class is additive and independently unit-tested
// (libs/oauth2/test) against fake repository/port implementations.

#include <authforge/common/ports/IAuditSink.h>
#include <authforge/common/ports/ICryptoProvider.h>
#include <authforge/common/ports/IRoleProvider.h>
#include <authforge/common/ports/ISubjectResolver.h>
#include <authforge/oauth2/jwk/JwkManager.h>
#include <authforge/oauth2/repository/IClientRepository.h>
#include <authforge/oauth2/repository/IGrantRepository.h>
#include <authforge/oauth2/repository/ITokenRepository.h>

#include <cstdint>
#include <functional>
#include <json/json.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace authforge::oauth2::protocol
{

/**
 * @brief Domain-layer OAuth2 token issuance/exchange/introspection/
 * revocation service (design.md's `TokenService`). See this header's top
 * comment for the full migration rationale and the one deliberate
 * behavior difference (PKCE verification via oauth2::pkce).
 *
 * Async chains here capture `shared_from_this()` at the outermost call
 * and thread the SAME captured `self` through every nested continuation
 * -- same lifetime-safety pattern the original class established (fixing
 * defect 1.9, a dangling-`this` use-after-free), and just as necessary
 * here since this class is likewise always owned via std::make_shared.
 */
class TokenService : public std::enable_shared_from_this<TokenService>
{
  public:
    TokenService(
      std::shared_ptr<authforge::oauth2::repository::IClientRepository> clients,
      std::shared_ptr<authforge::oauth2::repository::IGrantRepository> grants,
      std::shared_ptr<authforge::oauth2::repository::ITokenRepository> tokens,
      std::shared_ptr<authforge::common::ports::ICryptoProvider> crypto,
      std::shared_ptr<authforge::common::ports::IAuditSink> auditSink = nullptr,
      std::shared_ptr<authforge::common::ports::ISubjectResolver> subjectResolver = nullptr,
      std::shared_ptr<authforge::common::ports::IRoleProvider> roleProvider = nullptr,
      int64_t authCodeTtl = 600,
      int64_t accessTokenTtl = 3600,
      int64_t refreshTokenTtl = 2592000,
      std::string issuer = "http://localhost:5555"
    );

    /// Publish the JwkManager to use for id_token signing (openid scope).
    /// Same immutable-publish contract as the original: pass a
    /// shared_ptr<const JwkManager> that has already been init()'d
    /// exactly once.
    void setJwkManager(std::shared_ptr<const authforge::oauth2::JwkManager> jwkManager)
    {
        jwkManager_ = std::move(jwkManager);
    }

    /// Generate an Authorization Code. Original:
    /// oauth2::TokenService::generateAuthorizationCode.
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

    /// Exchange an authorization code for an access/refresh token pair.
    /// Original: oauth2::TokenService::exchangeCodeForToken.
    void exchangeCodeForToken(
      const std::string &code,
      const std::string &clientId,
      const std::string &clientSecret,
      const std::string &redirectUri,
      const std::string &codeVerifier,
      std::function<void(const Json::Value &)> &&callback
    );

    /// Refresh an access token (with reuse-detection cascade revocation).
    /// Original: oauth2::TokenService::refreshAccessToken.
    void refreshAccessToken(
      const std::string &refreshToken,
      const std::string &clientId,
      std::function<void(const Json::Value &)> &&callback
    );

    /// Validate an access token (not expired, not revoked). Original:
    /// oauth2::TokenService::validateAccessToken.
    void validateAccessToken(
      const std::string &token,
      std::function<void(std::shared_ptr<authforge::oauth2::model::OAuth2AccessToken>)> &&callback
    );

    /// Introspect a token per RFC 7662. Original:
    /// oauth2::TokenService::introspectToken.
    void introspectToken(
      const std::string &token,
      std::function<void(std::optional<authforge::oauth2::model::TokenIntrospection>)> &&callback
    );

    /// Revoke an access token per RFC 7009. Original:
    /// oauth2::TokenService::revokeAccessToken.
    void revokeAccessToken(
      const std::string &token,
      const std::string &revokedBy,
      std::function<void()> &&callback
    );

    /// Validate a PKCE code_verifier against a code_challenge/method.
    /// Delegates to oauth2::pkce::verifyCodeVerifier (RFC 7636 §4.6,
    /// conformant). Original: oauth2::TokenService::validatePkceCodeVerifier.
    bool validatePkceCodeVerifier(
      const std::string &codeVerifier,
      const std::string &codeChallenge,
      const std::string &codeChallengeMethod
    );

  private:
    std::shared_ptr<authforge::oauth2::repository::IClientRepository> clients_;
    std::shared_ptr<authforge::oauth2::repository::IGrantRepository> grants_;
    std::shared_ptr<authforge::oauth2::repository::ITokenRepository> tokens_;
    std::shared_ptr<authforge::common::ports::ICryptoProvider> crypto_;
    std::shared_ptr<authforge::common::ports::IAuditSink> auditSink_;
    std::shared_ptr<authforge::common::ports::ISubjectResolver> subjectResolver_;
    std::shared_ptr<authforge::common::ports::IRoleProvider> roleProvider_;
    int64_t authCodeTtl_;
    int64_t accessTokenTtl_;
    int64_t refreshTokenTtl_;
    std::string issuer_;
    std::shared_ptr<const authforge::oauth2::JwkManager> jwkManager_;

    /// Resolve `subject`'s roles via subjectResolver_ -> roleProvider_,
    /// invoking `cb` with an empty list if either port is unset or the
    /// subject does not resolve.
    void resolveRoles(
      const std::string &subject,
      std::function<void(std::vector<std::string>)> &&cb
    );

    void audit(
      const std::string &action,
      const std::string &outcome,
      const std::string &actorId,
      const std::string &targetType,
      const std::string &targetId
    );
};

}  // namespace authforge::oauth2::protocol
