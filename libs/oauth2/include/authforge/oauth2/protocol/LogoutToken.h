#pragma once

// B1 (OIDC Back-Channel Logout 1.0): pure construction of the logout_token
// JWT payload (spec §2.4). Lives in the Domain layer (libs/oauth2) because it
// is pure protocol logic -- no I/O, no signing (the caller signs the returned
// payload with JwkManager::signJwt), no Drogon. Keeping it pure makes the
// exact claim set trivially unit-testable.
//
// A logout_token tells an RP that an end-user's session ended at the OP. Per
// §2.4 it MUST carry iss/sub/aud/iat/exp/jti/events, MUST NOT carry nonce, and
// MUST carry sub OR sid (this OP issues sub only -- there is no sid concept;
// clients with backchannel_logout_session_required=true are a documented gap,
// see docs/productization-evolution/in-progress/backchannel-logout-design.md).

#include <cstdint>
#include <string>
#include <json/json.h>

namespace authforge::oauth2::protocol
{

/// OIDC Back-Channel Logout 1.0 §2.4 "events" claim member name. The member
/// MUST be present and map to an empty JSON object.
constexpr char kBackchannelLogoutEventUrn[] =
  "http://schemas.openid.net/event/backchannel-logout";

/// Default logout_token lifetime (seconds). §2.5 recommends a short lifetime;
/// 120s matches the spec's example upper bound.
constexpr int kLogoutTokenDefaultTtlSeconds = 120;

/// Generate a fresh, unique `jti` claim value (128-bit random, lowercase hex,
/// 32 chars). Uses OpenSSL RAND_bytes with a std::random_device + counter
/// fallback; never returns empty.
std::string generateJti();

/// Build the OIDC Back-Channel Logout 1.0 logout_token JWT payload (§2.4).
///
/// PURE and deterministic (the caller supplies `jti`, typically from
/// generateJti()): no I/O, no signing. Sign the returned payload with
/// JwkManager::signJwt().
///
/// @param issuer         OP issuer URL (iss).
/// @param subject        end-user subject string, == the id_token sub (sub).
/// @param audience       the RP's client_id this token is addressed to (aud).
/// @param issuedAtSeconds  iat (Unix seconds).
/// @param ttlSeconds     exp - iat (short; spec recommends <= 120s).
/// @param jti            unique token id (use generateJti()).
/// @return claims object: {iss, sub, aud, iat, exp, jti, events}. No nonce.
Json::Value buildLogoutTokenClaims(
  const std::string &issuer,
  const std::string &subject,
  const std::string &audience,
  std::int64_t issuedAtSeconds,
  int ttlSeconds,
  const std::string &jti);

}  // namespace authforge::oauth2::protocol
