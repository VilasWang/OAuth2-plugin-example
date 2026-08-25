#pragma once

// Task 17 slice 6 (fulla-sdk-refactor, design.md §6/§8's Data Models
// table): the `AuthorizationGrant` aggregate ("授权码 + PKCE + consent
// 上下文（对应现有 AuthorizationTransaction） | 单事务 save/consume").
// Wraps the existing AuthorizationTransaction DTO (Task 17 slice 2,
// model/Dto.h) with the PKCE-verification behavior a caller currently has
// to perform as a separate step (TokenService::exchangeCodeForToken
// manually calls validatePkceCodeVerifier against
// authCode->codeChallenge/codeChallengeMethod after fetching the DTO).
//
// Deliberately a thin wrapper (see Client.h's identical rationale) --
// holds an AuthorizationTransaction by value, does not duplicate its
// fields or re-model them with value objects.

#include <fulla/common/ports/ICryptoProvider.h>
#include <fulla/oauth2/model/Dto.h>
#include <fulla/oauth2/pkce/Pkce.h>

#include <string>

namespace fulla::oauth2::model
{

/**
 * @brief The `AuthorizationGrant` aggregate (design.md's Data Models
 * table): an AuthorizationTransaction DTO plus PKCE verification and the
 * expiry/consumed-state checks every consumer of a grant needs.
 */
class AuthorizationGrant
{
  public:
    explicit AuthorizationGrant(AuthorizationTransaction dto) : dto_(std::move(dto))
    {
    }

    const std::string &transactionId() const noexcept
    {
        return dto_.transactionId;
    }

    const std::string &clientId() const noexcept
    {
        return dto_.clientId;
    }

    const std::string &subject() const noexcept
    {
        return dto_.subject;
    }

    const std::string &redirectUri() const noexcept
    {
        return dto_.redirectUri;
    }

    const std::string &state() const noexcept
    {
        return dto_.state;
    }

    const std::vector<std::string> &requestedScopes() const noexcept
    {
        return dto_.requestedScopes;
    }

    const std::vector<std::string> &validScopes() const noexcept
    {
        return dto_.validScopes;
    }

    const std::vector<std::string> &consentRequiredScopes() const noexcept
    {
        return dto_.consentRequiredScopes;
    }

    bool consumed() const noexcept
    {
        return dto_.consumed;
    }

    int64_t expiresAt() const noexcept
    {
        return dto_.expiresAt;
    }

    /// True iff `nowSeconds` is at or past this grant's expiry.
    bool isExpired(int64_t nowSeconds) const noexcept
    {
        return nowSeconds > dto_.expiresAt;
    }

    /// True iff a PKCE code_challenge was recorded for this grant (i.e.
    /// the authorization request included PKCE parameters).
    bool hasPkceChallenge() const noexcept
    {
        return !dto_.codeChallenge.empty();
    }

    /// Verify `codeVerifier` against this grant's recorded code_challenge
    /// (RFC 7636 §4.6), via oauth2::pkce::verifyCodeVerifier. Only
    /// meaningful when hasPkceChallenge() is true -- callers must check
    /// that first (a grant with no recorded challenge has nothing to
    /// verify against; per RFC 7636 that decision -- e.g. whether PUBLIC
    /// clients are required to have used PKCE -- is a policy question for
    /// the caller, not this method).
    bool verifyPkceCodeVerifier(
      const std::string &codeVerifier,
      fulla::common::ports::ICryptoProvider &crypto
    ) const
    {
        fulla::common::model::PkceChallenge challenge(
          dto_.codeChallenge, dto_.codeChallengeMethod.empty() ? "plain" : dto_.codeChallengeMethod
        );
        return fulla::oauth2::pkce::verifyCodeVerifier(codeVerifier, challenge, crypto);
    }

    /// Access the underlying DTO (e.g. for repository persistence, which
    /// deals in DTOs, not aggregates).
    const AuthorizationTransaction &dto() const noexcept
    {
        return dto_;
    }

  private:
    AuthorizationTransaction dto_;
};

}  // namespace fulla::oauth2::model
