#pragma once

// Task 17 (authforge-sdk-refactor, design.md §6/§8 "protocol/"): small
// Domain-layer crypto helpers TokenService needs (secure token generation
// + at-rest token hashing), expressed purely in terms of
// authforge::common::ports::ICryptoProvider so this file has zero Drogon
// and zero OAuth2Plugin dependency.
//
// Mirrors OAuth2Plugin/include/oauth2/utils/CryptoUtils.h's
// generateSecureToken()/hashToken() free functions exactly (same
// algorithm: secureRandomBytes -> base64url for token generation;
// sha256Hex -> uppercase for at-rest hashing -- see that header's own
// extensive comment on why the hash case must stay UPPERCASE for
// exact-match token lookups). Duplicated here rather than reused via
// #include because CryptoUtils.h hardcodes a static
// authforge::drogon::adapters::OpenSslCryptoProvider instance, which is an
// Adapter-layer type this Domain-layer package must not depend on
// (design.md §4.1 rule 1); this version takes ICryptoProvider& as an
// explicit parameter instead, matching the oauth2::pkce module's own
// convention (see pkce/Pkce.h).

#include <authforge/common/ports/ICryptoProvider.h>

#include <string>

namespace authforge::oauth2::protocol
{

/**
 * @brief Generate a cryptographically secure random token, base64url
 * encoded (no padding). Default 32 bytes = 256 bits of entropy.
 */
std::string generateSecureToken(
  authforge::common::ports::ICryptoProvider &crypto,
  size_t bytes = 32
);

/**
 * @brief Hash a token for at-rest storage / exact-match lookup.
 * Returns an UPPERCASE hex SHA-256 digest (64 chars) -- see this header's
 * comment above for why the case must stay uppercase.
 */
std::string hashToken(
  authforge::common::ports::ICryptoProvider &crypto,
  const std::string &rawToken
);

}  // namespace authforge::oauth2::protocol
