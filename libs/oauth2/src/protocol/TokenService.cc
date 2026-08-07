#include <authforge/oauth2/protocol/TokenService.h>

#include <authforge/common/model/Subject.h>
#include <authforge/oauth2/pkce/Pkce.h>
#include <authforge/oauth2/protocol/TokenCrypto.h>

#include <chrono>

namespace authforge::oauth2::protocol
{

namespace
{
int64_t nowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch()
    )
      .count();
}

Json::Value makeError(const std::string &error, const std::string &desc = "")
{
    Json::Value json;
    json["error"] = error;
    if (!desc.empty())
        json["error_description"] = desc;
    return json;
}
}  // namespace

TokenService::TokenService(
  std::shared_ptr<authforge::oauth2::repository::IClientRepository> clients,
  std::shared_ptr<authforge::oauth2::repository::IGrantRepository> grants,
  std::shared_ptr<authforge::oauth2::repository::ITokenRepository> tokens,
  std::shared_ptr<authforge::common::ports::ICryptoProvider> crypto,
  std::shared_ptr<authforge::common::ports::IAuditSink> auditSink,
  std::shared_ptr<authforge::common::ports::ISubjectResolver> subjectResolver,
  std::shared_ptr<authforge::common::ports::IRoleProvider> roleProvider,
  int64_t authCodeTtl,
  int64_t accessTokenTtl,
  int64_t refreshTokenTtl,
  std::string issuer
)
    : clients_(std::move(clients)),
      grants_(std::move(grants)),
      tokens_(std::move(tokens)),
      crypto_(std::move(crypto)),
      auditSink_(std::move(auditSink)),
      subjectResolver_(std::move(subjectResolver)),
      roleProvider_(std::move(roleProvider)),
      authCodeTtl_(authCodeTtl),
      accessTokenTtl_(accessTokenTtl),
      refreshTokenTtl_(refreshTokenTtl),
      issuer_(std::move(issuer))
{
}

void TokenService::resolveRoles(
  const std::string &subject,
  std::function<void(std::vector<std::string>)> &&cb
)
{
    if (!roleProvider_)
    {
        cb({});
        return;
    }

    // Phase 4.5: prefer the subject-string role lookup when the provider
    // supports it -- preserves the legacy "roles keyed by subject string"
    // semantics (MemoryRoleRepository's userRoles_ map, populated from
    // admin_users config) byte-for-byte, without needing a subject_mapping
    // row for config-only subjects like "admin". Falls back to the pure
    // two-port chain (ISubjectResolver -> getRoles(int32)) otherwise.
    if (roleProvider_->supportsSubjectLookup())
    {
        roleProvider_->getRoles(subject, std::move(cb));
        return;
    }

    if (!subjectResolver_)
    {
        cb({});
        return;
    }

    authforge::common::model::Subject subjectValue(subject);
    auto roleProvider = roleProvider_;
    subjectResolver_->resolve(
      subjectValue,
      [roleProvider, cb = std::move(cb)](std::optional<int32_t> internalUserId) mutable {
          if (!internalUserId)
          {
              cb({});
              return;
          }
          roleProvider->getRoles(*internalUserId, std::move(cb));
      }
    );
}

void TokenService::audit(
  const std::string &action,
  const std::string &outcome,
  const std::string &actorId,
  const std::string &targetType,
  const std::string &targetId
)
{
    if (!auditSink_)
        return;

    authforge::common::observability::AuditEvent event;
    event.actorType = "user";
    event.actorId = actorId;
    event.action = action;
    event.targetType = targetType;
    event.targetId = targetId;
    event.outcome = outcome;
    auditSink_->record(event);
}

void TokenService::generateAuthorizationCode(
  const std::string &clientId,
  const std::string &subject,
  const std::string &scope,
  const std::string &redirectUri,
  const std::string &codeChallenge,
  const std::string &codeChallengeMethod,
  const std::string &nonce,
  std::function<void(bool, std::string, std::string)> &&callback
)
{
    if (!grants_ || !crypto_)
    {
        callback(false, "", "Storage not initialized");
        return;
    }

    auto code = generateSecureToken(*crypto_);
    authforge::oauth2::model::OAuth2AuthCode authCode;
    authCode.code = hashToken(*crypto_, code);
    authCode.clientId = clientId;
    authCode.userId = subject;
    authCode.scope = scope;
    authCode.redirectUri = redirectUri;
    authCode.codeChallenge = codeChallenge;
    authCode.codeChallengeMethod = codeChallengeMethod;
    authCode.nonce = nonce;
    authCode.expiresAt = nowSeconds() + authCodeTtl_;

    grants_->saveAuthCode(authCode, [callback = std::move(callback), code]() {
        callback(true, code, "");
    });
}

void TokenService::exchangeCodeForToken(
  const std::string &code,
  const std::string &clientId,
  const std::string &clientSecret,
  const std::string &redirectUri,
  const std::string &codeVerifier,
  std::function<void(const Json::Value &)> &&callback
)
{
    if (!clients_ || !grants_ || !tokens_ || !crypto_)
    {
        callback(makeError("server_error"));
        return;
    }

    // Same lifetime-safety pattern as the original (defect 1.9 fix): capture
    // `self` at the outermost async call and thread it through every nested
    // continuation.
    auto self = shared_from_this();
    clients_->validateClient(
      clientId,
      clientSecret,
      [self, this, code, clientId, redirectUri, codeVerifier, callback = std::move(callback)](
        bool isValid
      ) mutable {
          if (!isValid)
          {
              callback(makeError("invalid_client", "Client authentication failed"));
              return;
          }

          grants_->consumeAuthCode(
            hashToken(*crypto_, code),
            redirectUri,
            [self, this, callback = std::move(callback), clientId, codeVerifier](
              std::optional<authforge::oauth2::model::OAuth2AuthCode> authCodeOpt
            ) {
                if (!authCodeOpt)
                {
                    callback(makeError("invalid_grant", "Invalid authorization code"));
                    return;
                }
                if (authCodeOpt->clientId != clientId)
                {
                    callback(makeError("invalid_client", "Client ID mismatch"));
                    return;
                }

                if (!authCodeOpt->codeChallenge.empty())
                {
                    // PKCE was used - validate code_verifier.
                    if (
                      codeVerifier.empty() ||
                      !validatePkceCodeVerifier(
                        codeVerifier, authCodeOpt->codeChallenge, authCodeOpt->codeChallengeMethod
                      )
                    )
                    {
                        callback(makeError("invalid_grant", "PKCE validation failed"));
                        return;
                    }
                }
                // No advisory PUBLIC-client-without-PKCE warning here (the
                // original logs it via ILogger, which this Domain-layer port
                // does not depend on to avoid growing the constructor
                // parameter list for a warning-only side channel; full
                // enforcement happens at /oauth2/authorize time regardless).

                auto now = nowSeconds();
                if (now > authCodeOpt->expiresAt)
                {
                    callback(makeError("invalid_grant", "Code expired"));
                    return;
                }

                auto authCode = *authCodeOpt;
                resolveRoles(
                  authCode.userId,
                  [self, this, callback, authCode, now](std::vector<std::string> roles) {
                      Json::Value rolesJson(Json::arrayValue);
                      for (const auto &r : roles)
                          rolesJson.append(r);

                      auto tokenStr = generateSecureToken(*crypto_);
                      authforge::oauth2::model::OAuth2AccessToken token;
                      token.token = hashToken(*crypto_, tokenStr);
                      token.clientId = authCode.clientId;
                      token.userId = authCode.userId;
                      token.scope = authCode.scope;
                      // P2 #10: record the real issue time so introspection's
                      // iat (RFC 7662 §2.2) is populated, not left at the 0
                      // default (which the introspect endpoint silently omits).
                      token.issuedAt = now;
                      token.expiresAt = now + accessTokenTtl_;

                      auto refreshTokenStr = generateSecureToken(*crypto_);
                      auto familyId = generateSecureToken(*crypto_, 16);
                      authforge::oauth2::model::OAuth2RefreshToken refreshToken;
                      refreshToken.token = hashToken(*crypto_, refreshTokenStr);
                      refreshToken.accessToken = token.token;
                      refreshToken.clientId = authCode.clientId;
                      refreshToken.userId = authCode.userId;
                      refreshToken.scope = authCode.scope;
                      refreshToken.expiresAt = now + refreshTokenTtl_;
                      refreshToken.familyId = familyId;

                      tokens_->saveTokenPair(
                        token,
                        refreshToken,
                        [self,
                         this,
                         callback,
                         tokenStr,
                         refreshTokenStr,
                         rolesJson,
                         authCode,
                         now](bool ok) {
                            if (!ok)
                            {
                                // Persistence failed: the tokens we would
                                // hand out were never stored, so issuing
                                // them anyway would be a silent failure
                                // (introspection/refresh lookups all miss).
                                audit(
                                  "token_issued",
                                  "failure",
                                  authCode.userId,
                                  "token",
                                  ""
                                );
                                callback(makeError("server_error", "Failed to persist token pair"));
                                return;
                            }
                            Json::Value json;
                            json["access_token"] = tokenStr;
                            json["token_type"] = "Bearer";
                            // P1 #6: advertise the real configured lifetime, not a
                            // hardcoded 3600 (RFC 6749 §5.1 requires expires_in to
                            // be the token's actual remaining lifetime).
                            json["expires_in"] = (Json::Int64)(accessTokenTtl_);
                            json["refresh_token"] = refreshTokenStr;
                            json["roles"] = rolesJson;

                            if (
                              jwkManager_ && jwkManager_->isInitialized() &&
                              authCode.scope.find("openid") != std::string::npos
                            )
                            {
                                Json::Value idTokenClaims;
                                idTokenClaims["iss"] = issuer_;
                                idTokenClaims["sub"] = authCode.userId;
                                idTokenClaims["aud"] = authCode.clientId;
                                idTokenClaims["iat"] = (Json::Int64)now;
                                // P1 #6: id_token exp follows the access token TTL
                                // (OIDC Core §2 requires exp to be the real expiry).
                                idTokenClaims["exp"] = (Json::Int64)(now + accessTokenTtl_);
                                if (!authCode.nonce.empty())
                                {
                                    idTokenClaims["nonce"] = authCode.nonce;
                                }

                                std::string idToken = jwkManager_->signJwt(idTokenClaims);
                                if (!idToken.empty())
                                {
                                    json["id_token"] = idToken;
                                }
                            }

                            audit("token_issued", "success", authCode.userId, "token", "");
                            callback(json);
                        }
                      );
                  }
                );
            }
          );
      }
    );
}

void TokenService::refreshAccessToken(
  const std::string &refreshTokenStr,
  const std::string &clientId,
  std::function<void(const Json::Value &)> &&callback
)
{
    if (!tokens_ || !crypto_)
    {
        callback(makeError("server_error"));
        return;
    }

    auto hashedRt = hashToken(*crypto_, refreshTokenStr);

    auto self = shared_from_this();
    tokens_->atomicRevokeRefreshToken(
      hashedRt,
      [self, this, callback = std::move(callback), clientId, hashedRt](
        std::optional<authforge::oauth2::model::OAuth2RefreshToken> storedRt
      ) mutable {
          if (!storedRt)
          {
              tokens_->getRefreshToken(
                hashedRt,
                [self, this, callback = std::move(callback)](
                  std::optional<authforge::oauth2::model::OAuth2RefreshToken> maybeRevoked
                ) {
                    if (maybeRevoked && maybeRevoked->revoked && !maybeRevoked->familyId.empty())
                    {
                        audit(
                          "refresh_token_reuse_detected",
                          "failure",
                          maybeRevoked->userId,
                          "token_family",
                          maybeRevoked->familyId
                        );
                        tokens_->revokeTokenFamily(maybeRevoked->familyId, [callback]() {
                            callback(makeError("invalid_grant", "Token reuse detected"));
                        });
                    }
                    else
                    {
                        callback(makeError("invalid_grant", "Invalid or revoked refresh token"));
                    }
                }
              );
              return;
          }

          if (storedRt->clientId != clientId)
          {
              callback(makeError("invalid_grant", "Client mismatch"));
              return;
          }

          auto now = nowSeconds();
          if (now > storedRt->expiresAt)
          {
              callback(makeError("invalid_grant", "Token expired"));
              return;
          }

          auto newTokenStr = generateSecureToken(*crypto_);
          authforge::oauth2::model::OAuth2AccessToken token;
          token.token = hashToken(*crypto_, newTokenStr);
          token.clientId = storedRt->clientId;
          token.userId = storedRt->userId;
          token.scope = storedRt->scope;
          // P2 #10: record real issue time for introspection iat.
          token.issuedAt = now;
          token.expiresAt = now + accessTokenTtl_;

          auto newRefreshTokenStr = generateSecureToken(*crypto_);
          authforge::oauth2::model::OAuth2RefreshToken newRt;
          newRt.token = hashToken(*crypto_, newRefreshTokenStr);
          newRt.accessToken = token.token;
          newRt.clientId = storedRt->clientId;
          newRt.userId = storedRt->userId;
          newRt.scope = storedRt->scope;
          newRt.expiresAt = now + refreshTokenTtl_;
          newRt.familyId = storedRt->familyId;

          tokens_->saveTokenPair(
            token, newRt, [self, this, callback, newTokenStr, newRefreshTokenStr, storedRt](bool ok) {
                if (!ok)
                {
                    // Same silent-failure guard as exchangeCodeForToken:
                    // never hand out a rotated pair that was never stored.
                    callback(makeError("server_error", "Failed to persist token pair"));
                    return;
                }
                audit("token_refreshed", "success", storedRt->userId, "token", "");
                Json::Value json;
                json["access_token"] = newTokenStr;
                json["token_type"] = "Bearer";
                // P1 #6: real configured lifetime, not hardcoded 3600.
                json["expires_in"] = (Json::Int64)accessTokenTtl_;
                json["refresh_token"] = newRefreshTokenStr;
                callback(json);
            }
          );
      }
    );
}

void TokenService::validateAccessToken(
  const std::string &token,
  std::function<void(std::shared_ptr<authforge::oauth2::model::OAuth2AccessToken>)> &&callback
)
{
    if (!tokens_ || !crypto_)
    {
        callback(nullptr);
        return;
    }

    auto hashedToken = hashToken(*crypto_, token);
    // P1 #8 (评审问题点 8, intentional): this async callback captures only
    // [callback] and references neither `this` nor any member -- the body uses
    // only the parameter `t` and the free function nowSeconds(). shared_from_this
    // is therefore deliberately NOT captured (it would be dead weight). If a
    // future change adds member access inside this callback, switch to capturing
    // `auto self = shared_from_this()` to keep lifetime safety -- that is the
    // real latent risk here, not a current one.
    tokens_->getAccessToken(
      hashedToken, [callback](std::optional<authforge::oauth2::model::OAuth2AccessToken> t) {
          if (!t || t->revoked)
          {
              callback(nullptr);
              return;
          }

          if (nowSeconds() > t->expiresAt)
          {
              callback(nullptr);
              return;
          }

          callback(std::make_shared<authforge::oauth2::model::OAuth2AccessToken>(*t));
      }
    );
}

void TokenService::introspectToken(
  const std::string &token,
  std::function<void(std::optional<authforge::oauth2::model::TokenIntrospection>)> &&callback
)
{
    if (!tokens_ || !crypto_)
    {
        callback(std::nullopt);
        return;
    }
    auto hashedToken = hashToken(*crypto_, token);
    tokens_->introspectToken(hashedToken, std::move(callback));
}

void TokenService::revokeAccessToken(
  const std::string &token,
  const std::string &revokedBy,
  std::function<void()> &&callback
)
{
    if (!tokens_ || !crypto_)
    {
        if (callback)
            callback();
        return;
    }
    auto hashedToken = hashToken(*crypto_, token);
    tokens_->revokeAccessToken(hashedToken, revokedBy, [callback = std::move(callback)]() {
        if (callback)
            callback();
    });
}

bool TokenService::validatePkceCodeVerifier(
  const std::string &codeVerifier,
  const std::string &codeChallenge,
  const std::string &codeChallengeMethod
)
{
    if (!crypto_)
        return false;

    authforge::common::model::PkceChallenge challenge(
      codeChallenge, codeChallengeMethod.empty() ? "plain" : codeChallengeMethod
    );
    return authforge::oauth2::pkce::verifyCodeVerifier(codeVerifier, challenge, *crypto_);
}

}  // namespace authforge::oauth2::protocol
