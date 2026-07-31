#pragma once

// Task 17 (authforge-sdk-refactor, design.md §8/§17): libs/oauth2 Domain
// layer, oauth2::pkce module ("PKCE、JWK 签名（经 ICryptoProvider 端口）").
//
// This is a pure-function port of the PKCE (RFC 7636) logic currently
// duplicated across OAuth2Plugin/include/oauth2/utils/CryptoUtils.h
// (computeCodeChallenge/isValidCodeVerifier/isValidCodeChallenge) and
// OAuth2Plugin/src/services/TokenService.cc
// (validatePkceCodeVerifier/generateSha256Hash). Unlike CryptoUtils.h
// (which hardcodes a static authforge::drogon::adapters::OpenSslCryptoProvider
// instance -- fine for an Adapter-layer convenience header, but not
// allowed here), every function below takes an
// authforge::common::ports::ICryptoProvider& as an explicit parameter:
// this is the Domain layer (design.md §4.1 rule 1: no Drogon; and by
// extension here, no OAuth2Plugin/adapters/* either, since libs/oauth2
// must not depend on OAuth2Plugin -- the dependency direction is the
// other way: OAuth2Plugin/apps/server eventually depends on libs/oauth2,
// per design.md §6's target layout).
//
// verifyCodeVerifier() intentionally implements the CORRECT RFC 7636 §4.6
// algorithm (base64url of the RAW DIGEST BYTES for S256), NOT the
// non-conformant behavior discovered in
// TokenService::generateSha256Hash() during Task 14 (which base64s the
// ASCII TEXT of the hex digest string -- see that function's own
// extensive comment, PROGRESS.md, and the Task 14 slice 6 commit message
// for the full writeup of that pre-existing, deliberately-preserved-as-is
// production defect). This module does NOT call generateSha256Hash() and
// is not affected by that defect; it is a fresh, spec-correct
// implementation for the NEW libs/oauth2 Domain package. Wiring
// TokenService/AuthorizationService to actually USE this module instead
// of the old CryptoUtils.h-based logic (and thus fixing the RFC
// conformance issue as a side effect, or deliberately choosing not to for
// backward compatibility) is a decision for the TokenService/
// AuthorizationService migration slice, not this one -- this slice only
// establishes the correct, standalone, tested primitive.

#include <authforge/common/model/PkceChallenge.h>
#include <authforge/common/ports/ICryptoProvider.h>

#include <string>

namespace authforge::oauth2::pkce
{

/**
 * @brief Compute the RFC 7636 §4.2 code_challenge from a code_verifier.
 *
 * - method "S256": BASE64URL(SHA256(ASCII(codeVerifier))) -- base64url of
 *   the raw 32-byte digest, per spec.
 * - method "plain": codeVerifier itself, unchanged.
 *
 * @param codeVerifier The client's code_verifier (should satisfy
 * isValidCodeVerifierFormat(), but this function does not itself enforce
 * that -- callers needing format validation should call it explicitly).
 * @param method "S256" or "plain".
 * @param crypto Crypto provider used for the S256 hash + base64url encode.
 * @return The computed code_challenge.
 */
std::string computeCodeChallenge(
  const std::string &codeVerifier,
  const std::string &method,
  authforge::common::ports::ICryptoProvider &crypto
);

/**
 * @brief Verify that `codeVerifier` matches `challenge` per RFC 7636 §4.6.
 *
 * Recomputes the code_challenge from `codeVerifier` using `challenge`'s
 * method and compares it against `challenge`'s stored value.
 *
 * @param codeVerifier The code_verifier presented at the token endpoint.
 * @param challenge The code_challenge + method recorded at the
 * authorization endpoint.
 * @param crypto Crypto provider used for the S256 hash + base64url encode.
 * @return true iff the recomputed challenge matches.
 */
bool verifyCodeVerifier(
  const std::string &codeVerifier,
  const authforge::common::model::PkceChallenge &challenge,
  authforge::common::ports::ICryptoProvider &crypto
);

/**
 * @brief Validate RFC 7636 §4.1 code_verifier format: 43-128 characters,
 * charset [A-Za-z0-9-._~]. Pure function, no crypto needed.
 */
bool isValidCodeVerifierFormat(const std::string &codeVerifier);

/**
 * @brief Validate RFC 7636 §4.2 code_challenge format: 43-128 characters,
 * charset [A-Za-z0-9-._~]. Pure function, no crypto needed.
 */
bool isValidCodeChallengeFormat(const std::string &codeChallenge);

}  // namespace authforge::oauth2::pkce
