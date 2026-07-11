#pragma once

// Task 17 slice 6 (authforge-sdk-refactor, design.md §6/§8's Data Models
// table): the `TokenPair` aggregate ("access token + refresh token（+
// familyId） | saveTokenPair 原子；revokeTokenFamily 级联"). Wraps the
// existing OAuth2AccessToken + OAuth2RefreshToken DTOs (Task 17 slice 2,
// model/Dto.h), enforcing at construction time the ONE invariant every
// production call site (TokenService::exchangeCodeForToken/
// refreshAccessToken) already relies on but expresses only by convention:
// the two tokens in a pair must share the same clientId/userId/scope, and
// the refresh token's `accessToken` field must reference the access
// token's own (hashed) value -- i.e. they are genuinely a PAIR, not two
// independently-constructed tokens that happen to be saved together.
//
// Deliberately a thin wrapper (see Client.h's identical rationale) --
// holds the two DTOs by value, does not duplicate their fields.

#include <authforge/oauth2/model/Dto.h>

#include <stdexcept>

namespace authforge::oauth2::model
{

/**
 * @brief The `TokenPair` aggregate (design.md's Data Models table): an
 * access token + refresh token that were issued together and share a
 * family (for reuse-detection cascade revocation).
 */
class TokenPair
{
  public:
    /// Constructs a TokenPair, validating the pairing invariant.
    /// @throws std::invalid_argument if `accessToken`/`refreshToken` do
    /// not reference each other (refreshToken.accessToken must equal
    /// accessToken.token) or have mismatched clientId/userId.
    TokenPair(OAuth2AccessToken accessToken, OAuth2RefreshToken refreshToken)
        : accessToken_(std::move(accessToken)), refreshToken_(std::move(refreshToken))
    {
        if (refreshToken_.accessToken != accessToken_.token)
        {
            throw std::invalid_argument(
              "TokenPair: refreshToken.accessToken must reference accessToken.token"
            );
        }
        if (refreshToken_.clientId != accessToken_.clientId)
        {
            throw std::invalid_argument(
              "TokenPair: clientId mismatch between access/refresh token"
            );
        }
        if (refreshToken_.userId != accessToken_.userId)
        {
            throw std::invalid_argument("TokenPair: userId mismatch between access/refresh token");
        }
    }

    const OAuth2AccessToken &accessToken() const noexcept
    {
        return accessToken_;
    }

    const OAuth2RefreshToken &refreshToken() const noexcept
    {
        return refreshToken_;
    }

    const std::string &clientId() const noexcept
    {
        return accessToken_.clientId;
    }

    const std::string &userId() const noexcept
    {
        return accessToken_.userId;
    }

    const std::string &scope() const noexcept
    {
        return accessToken_.scope;
    }

    const std::string &familyId() const noexcept
    {
        return refreshToken_.familyId;
    }

  private:
    OAuth2AccessToken accessToken_;
    OAuth2RefreshToken refreshToken_;
};

}  // namespace authforge::oauth2::model
