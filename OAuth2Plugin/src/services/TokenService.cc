#include <oauth2/services/TokenService.h>
#include <oauth2/utils/SubjectGenerator.h>
#include <oauth2/utils/CryptoUtils.h>
#include <oauth2/utils/JwkManager.h>
#include <oauth2/observability/AuditLogger.h>
#include <oauth2/adapters/DrogonLogger.h>
#include <oauth2/adapters/OpenSslCryptoProvider.h>
#include <cctype>
#include <chrono>

namespace
{
// Task 14 (design.md §5.6): shared ILogger instance backing this file's two
// LOG_WARN call sites, replacing direct Drogon LOG_* macro usage. See
// JwkManager.cc for the identical pattern and DrogonLogger.h's own comment
// for why an Adapter-layer class (this file, which still calls
// drogon::app().getCustomConfig() elsewhere and is not yet fully
// Drogon-free) using a Drogon-backed ILogger implementation is expected and
// correct.
authforge::common::ports::ILogger &logger()
{
    static oauth2::adapters::DrogonLogger instance;
    return instance;
}
}  // namespace

namespace oauth2
{

TokenService::TokenService(
  std::shared_ptr<IOAuth2Storage> storage,
  int64_t authCodeTtl,
  int64_t accessTokenTtl,
  int64_t refreshTokenTtl
)
    : storage_(std::move(storage)),
      authCodeTtl_(authCodeTtl),
      accessTokenTtl_(accessTokenTtl),
      refreshTokenTtl_(refreshTokenTtl)
{
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
    if (!storage_)
    {
        callback(false, "", "Storage not initialized");
        return;
    }

    auto code = utils::generateSecureToken();
    OAuth2AuthCode authCode;
    authCode.code = utils::hashToken(code);
    authCode.clientId = clientId;
    authCode.userId = subject;
    authCode.scope = scope;
    authCode.redirectUri = redirectUri;
    authCode.codeChallenge = codeChallenge;
    authCode.codeChallengeMethod = codeChallengeMethod;
    authCode.nonce = nonce;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch()
    )
                 .count();
    authCode.expiresAt = now + authCodeTtl_;

    storage_->saveAuthCode(authCode, [callback = std::move(callback), code]() {
        callback(true, code, "");
    });
}

static Json::Value makeError(const std::string &error, const std::string &desc = "")
{
    Json::Value json;
    json["error"] = error;
    if (!desc.empty())
        json["error_description"] = desc;
    return json;
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
    if (!storage_)
    {
        callback(makeError("server_error"));
        return;
    }

    // Defect 1.9 fix: capture `self` (shared owner) at the OUTERMOST async call
    // and thread the SAME `self` through every nested continuation below, so
    // the service stays alive until the in-flight callback completes. `this` is
    // kept alongside `self` purely for unchanged member access (`storage_` /
    // accessTokenTtl_ / refreshTokenTtl_ / jwkManager_); `self` guarantees that
    // `this` never dangles.
    auto self = shared_from_this();
    storage_->validateClient(
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

          storage_->consumeAuthCode(
            utils::hashToken(code),
            redirectUri,
            [self, this, callback = std::move(callback), clientId, code, codeVerifier](
              std::optional<OAuth2AuthCode> authCode
            ) {
                if (!authCode)
                {
                    callback(makeError("invalid_grant", "Invalid authorization code"));
                    return;
                }
                if (authCode->clientId != clientId)
                {
                    callback(makeError("invalid_client", "Client ID mismatch"));
                    return;
                }

                if (!authCode->codeChallenge.empty())
                {
                    // PKCE was used - validate code_verifier
                    if (
                      codeVerifier.empty() ||
                      !validatePkceCodeVerifier(
                        codeVerifier, authCode->codeChallenge, authCode->codeChallengeMethod
                      )
                    )
                    {
                        callback(makeError("invalid_grant", "PKCE validation failed"));
                        return;
                    }
                }
                else
                {
                    // No PKCE was used during authorization
                    // For PUBLIC clients, PKCE is mandatory (OAuth 2.1)
                    // We enforce this by checking if client is PUBLIC and code_verifier is missing
                    storage_->getClient(clientId, [](std::optional<OAuth2Client> client) {
                        // Note: enforcement is advisory here - the auth code was already consumed
                        // Full enforcement should happen at /oauth2/authorize time
                        if (client && client->clientType == ClientType::PUBLIC)
                        {
                            logger().log(
                              authforge::common::ports::LogLevel::Warn,
                              "[SECURITY] PUBLIC client " + client->clientId +
                                " used authorization code without PKCE"
                            );
                        }
                    });
                }

                auto now = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch()
                )
                             .count();

                if (now > authCode->expiresAt)
                {
                    callback(makeError("invalid_grant", "Code expired"));
                    return;
                }

                storage_->getUserRoles(
                  authCode->userId,
                  [self, this, callback, authCode, now](std::vector<std::string> roles) {
                      Json::Value rolesJson(Json::arrayValue);
                      for (const auto &r : roles)
                          rolesJson.append(r);

                      auto tokenStr = utils::generateSecureToken();
                      OAuth2AccessToken token;
                      token.token = utils::hashToken(tokenStr);
                      token.clientId = authCode->clientId;
                      token.userId = authCode->userId;
                      token.scope = authCode->scope;
                      token.expiresAt = now + accessTokenTtl_;

                      auto refreshTokenStr = utils::generateSecureToken();
                      auto familyId = utils::generateSecureToken(16);  // New family
                      OAuth2RefreshToken refreshToken;
                      refreshToken.token = utils::hashToken(refreshTokenStr);
                      refreshToken.accessToken = token.token;
                      refreshToken.clientId = authCode->clientId;
                      refreshToken.userId = authCode->userId;
                      refreshToken.scope = authCode->scope;
                      refreshToken.expiresAt = now + refreshTokenTtl_;
                      refreshToken.familyId = familyId;

                      storage_->saveTokenPair(
                        token,
                        refreshToken,
                        [self,
                         this,
                         callback,
                         tokenStr,
                         refreshTokenStr,
                         rolesJson,
                         authCode,
                         now]() {
                            Json::Value json;
                            json["access_token"] = tokenStr;
                            json["token_type"] = "Bearer";
                            json["expires_in"] = (Json::Int64)(3600);
                            json["refresh_token"] = refreshTokenStr;
                            json["roles"] = rolesJson;

                            // Issue id_token if scope includes "openid"
                            if (
                              jwkManager_ && jwkManager_->isInitialized() &&
                              authCode->scope.find("openid") != std::string::npos
                            )
                            {
                                auto customConfig = drogon::app().getCustomConfig();
                                std::string issuer = "http://localhost:5555";
                                if (
                                  customConfig.isMember("metadata") &&
                                  customConfig["metadata"].isMember("issuer")
                                )
                                {
                                    issuer = customConfig["metadata"]["issuer"].asString();
                                }

                                Json::Value idTokenClaims;
                                idTokenClaims["iss"] = issuer;
                                idTokenClaims["sub"] = authCode->userId;
                                idTokenClaims["aud"] = authCode->clientId;
                                idTokenClaims["iat"] = (Json::Int64)now;
                                idTokenClaims["exp"] = (Json::Int64)(now + 3600);
                                if (!authCode->nonce.empty())
                                {
                                    idTokenClaims["nonce"] = authCode->nonce;
                                }

                                std::string idToken = jwkManager_->signJwt(idTokenClaims);
                                if (!idToken.empty())
                                {
                                    json["id_token"] = idToken;
                                }
                            }

                            oauth2::observability::AuditLogger::log(
                              "token_issued", "success", nullptr, authCode->userId, "token", ""
                            );
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
    if (!storage_)
    {
        callback(makeError("server_error"));
        return;
    }

    auto hashedRt = utils::hashToken(refreshTokenStr);

    // Atomic CAS: revoke the old RT and get its data
    // If it's already revoked, this means reuse -> cascade revoke family
    //
    // Defect 1.9 fix: capture `self` (shared owner) at the OUTERMOST async call
    // and thread the SAME `self` through every nested continuation, so the
    // service stays alive until the in-flight callback completes. `this` is
    // kept for unchanged member access (`storage_` / accessTokenTtl_ /
    // refreshTokenTtl_); `self` guarantees `this` never dangles.
    auto self = shared_from_this();
    storage_->atomicRevokeRefreshToken(
      hashedRt,
      [self, this, callback = std::move(callback), clientId, hashedRt](
        std::optional<OAuth2RefreshToken> storedRt
      ) mutable {
          if (!storedRt)
          {
              // Token not found OR already revoked -> possible reuse attack
              // Try to get the token to check if it exists but is revoked
              storage_->getRefreshToken(
                hashedRt,
                [self,
                 this,
                 callback = std::move(callback)](std::optional<OAuth2RefreshToken> maybeRevoked) {
                    if (maybeRevoked && maybeRevoked->revoked && !maybeRevoked->familyId.empty())
                    {
                        // REUSE DETECTED! Cascade revoke the entire family
                        logger().log(
                          authforge::common::ports::LogLevel::Warn,
                          "[SECURITY] Refresh token reuse detected! Revoking token family: " +
                            maybeRevoked->familyId
                        );
                        oauth2::observability::AuditLogger::log(
                          "refresh_token_reuse_detected",
                          "failure",
                          nullptr,
                          maybeRevoked->userId,
                          "token_family",
                          maybeRevoked->familyId
                        );
                        storage_->revokeTokenFamily(maybeRevoked->familyId, [callback]() {
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

          // Normal path: token was valid and is now revoked
          if (storedRt->clientId != clientId)
          {
              callback(makeError("invalid_grant", "Client mismatch"));
              return;
          }

          auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch()
          )
                       .count();

          if (now > storedRt->expiresAt)
          {
              callback(makeError("invalid_grant", "Token expired"));
              return;
          }

          // Issue new token pair, inheriting the family
          auto newTokenStr = utils::generateSecureToken();
          OAuth2AccessToken token;
          token.token = utils::hashToken(newTokenStr);
          token.clientId = storedRt->clientId;
          token.userId = storedRt->userId;
          token.scope = storedRt->scope;
          token.expiresAt = now + accessTokenTtl_;

          auto newRefreshTokenStr = utils::generateSecureToken();
          OAuth2RefreshToken newRt;
          newRt.token = utils::hashToken(newRefreshTokenStr);
          newRt.accessToken = token.token;
          newRt.clientId = storedRt->clientId;
          newRt.userId = storedRt->userId;
          newRt.scope = storedRt->scope;
          newRt.expiresAt = now + refreshTokenTtl_;
          newRt.familyId = storedRt->familyId;  // Inherit family

          storage_
            ->saveTokenPair(token, newRt, [callback, newTokenStr, newRefreshTokenStr, storedRt]() {
                oauth2::observability::AuditLogger::log(
                  "token_refreshed", "success", nullptr, storedRt->userId, "token", ""
                );
                Json::Value json;
                json["access_token"] = newTokenStr;
                json["token_type"] = "Bearer";
                json["expires_in"] = (Json::Int64)3600;
                json["refresh_token"] = newRefreshTokenStr;
                callback(json);
            });
      }
    );
}

void TokenService::validateAccessToken(
  const std::string &token,
  std::function<void(std::shared_ptr<OAuth2AccessToken>)> &&callback
)
{
    if (!storage_)
    {
        callback(nullptr);
        return;
    }

    auto hashedToken = utils::hashToken(token);
    storage_->getAccessToken(hashedToken, [callback](std::optional<OAuth2AccessToken> t) {
        if (!t || t->revoked)
        {
            callback(nullptr);
            return;
        }

        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch()
        )
                     .count();

        if (now > t->expiresAt)
        {
            callback(nullptr);
            return;
        }

        callback(std::make_shared<OAuth2AccessToken>(*t));
    });
}

void TokenService::introspectToken(
  const std::string &token,
  std::function<void(std::optional<TokenIntrospection>)> &&callback
)
{
    if (!storage_)
    {
        callback(std::nullopt);
        return;
    }
    auto hashedToken = utils::hashToken(token);
    storage_->introspectToken(hashedToken, std::move(callback));
}

void TokenService::revokeAccessToken(
  const std::string &token,
  const std::string &revokedBy,
  std::function<void()> &&callback
)
{
    if (!storage_)
    {
        if (callback)
            callback();
        return;
    }
    auto hashedToken = utils::hashToken(token);
    storage_->revokeAccessToken(hashedToken, revokedBy, [callback = std::move(callback)]() {
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
    std::string method = codeChallengeMethod.empty() ? "plain" : codeChallengeMethod;

    if (method == "plain")
    {
        return codeVerifier == codeChallenge;
    }
    else if (method == "S256")
    {
        return generateSha256Hash(codeVerifier) == codeChallenge;
    }
    return false;
}

std::string TokenService::generateSha256Hash(const std::string &input)
{
    // Task 14 (design.md §5.6): migrated off drogon::utils::getSha256 /
    // drogon::utils::base64Encode onto OpenSslCryptoProvider.
    //
    // IMPORTANT -- this function's behavior is preserved BYTE-FOR-BYTE
    // exactly as it was pre-migration, including a pre-existing, NOT-fixed
    // RFC 7636 non-compliance this migration discovered but is explicitly
    // out of scope to fix here (Task 14 is "remove drogon::utils", not "fix
    // PKCE protocol bugs" -- deviating would be an unauthorized behavior
    // change). RFC 7636 §4.2 defines
    // code_challenge = BASE64URL(SHA256(ASCII(code_verifier))) -- i.e.
    // base64url of the RAW 32 DIGEST BYTES. This function instead base64s
    // the ASCII TEXT of the hex digest STRING (64 ASCII chars), which is a
    // different value than any RFC-7636-conformant client (browser SDKs,
    // mobile SDKs, etc.) would compute for the same code_verifier. This
    // does not surface as a test failure today because the one existing
    // PKCE test (P0FunctionalityTest.cc's Unit_P0_PKCE_Legacy_Hashing) is
    // self-consistent: it generates the challenge via this SAME function
    // and validates via this SAME function, so it round-trips regardless
    // of RFC conformance. This is a real interoperability defect for
    // standard-conformant PKCE clients and should be tracked/fixed as its
    // own dedicated bugfix task, not folded into this Drogon-dependency
    // removal.
    //
    // Reproducing the pre-migration case-sensitive detail precisely: the
    // ASCII hex string that gets base64-encoded must be UPPERCASE to match
    // byte-for-byte, because drogon::utils::getSha256() returned uppercase
    // hex (verified in OpenSslCryptoProviderTest.cc's cross-check) and this
    // function base64-encodes that hex STRING's ASCII bytes, not the
    // decoded digest -- a lowercase hex string would base64-encode to a
    // completely different result.
    static oauth2::adapters::OpenSslCryptoProvider cryptoProvider;
    std::string hexUpper = cryptoProvider.sha256Hex(input);
    for (char &c : hexUpper)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    return cryptoProvider.base64UrlEncode(hexUpper);
}

}  // namespace oauth2
