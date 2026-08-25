#pragma once

// Task 13 (fulla-sdk-refactor, design.md §3.1/§5.1/§6): value objects
// for the shared Domain kernel. PkceChallenge bundles a PKCE (RFC 7636)
// code_challenge with its code_challenge_method, mirroring the pair the
// existing AuthorizationTransaction model already carries
// (codeChallenge/codeChallengeMethod fields) but as a single cohesive value
// instead of two independently-passable strings that could be mismatched
// at a call site.
//
// This type deliberately does NOT perform the actual PKCE verification
// (comparing a code_verifier against the challenge via S256/plain) --
// that's a stateful crypto operation belonging to oauth2::pkce (a later
// M2b task, per design.md's directory layout: "libs/oauth2/include/
// fulla/oauth2/pkce/"), which will consume ICryptoProvider (Task 14).
// This value object's only job is to hold the two strings together and
// reject a method this codebase does not support, so an invalid method
// cannot silently reach the verification step.

#include <stdexcept>
#include <string>
#include <utility>

namespace fulla::common::model
{

/**
 * @brief PKCE code_challenge + code_challenge_method pair (RFC 7636 §4.3).
 */
class PkceChallenge
{
  public:
    /// Construct from a code_challenge and its code_challenge_method
    /// ("S256" or "plain", per RFC 7636 §4.3). Throws std::invalid_argument
    /// if `challenge` is empty or `method` is not one of the two supported
    /// values.
    PkceChallenge(std::string challenge, std::string method)
        : challenge_(std::move(challenge)), method_(std::move(method))
    {
        if (challenge_.empty())
        {
            throw std::invalid_argument("PkceChallenge: challenge must not be empty");
        }
        if (method_ != "S256" && method_ != "plain")
        {
            throw std::invalid_argument(
              "PkceChallenge: method must be 'S256' or 'plain', got '" + method_ + "'"
            );
        }
    }

    const std::string &challenge() const noexcept
    {
        return challenge_;
    }

    const std::string &method() const noexcept
    {
        return method_;
    }

    bool isS256() const noexcept
    {
        return method_ == "S256";
    }

    bool operator==(const PkceChallenge &other) const noexcept
    {
        return challenge_ == other.challenge_ && method_ == other.method_;
    }

    bool operator!=(const PkceChallenge &other) const noexcept
    {
        return !(*this == other);
    }

  private:
    std::string challenge_;
    std::string method_;
};

}  // namespace fulla::common::model
