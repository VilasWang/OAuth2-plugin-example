#pragma once

// M2.5 identity completion (fulla-sdk-refactor, design.md §5.1/§6):
// real (non-placeholder) implementation. Ports the former
// fulla::common::utils::TotpUtils static class into the Domain layer,
// unchanged algorithm (RFC 6238 TOTP over
// HMAC-SHA1, 30-second time steps, 6-digit codes; RFC 4226 dynamic
// truncation), but expressed as free functions taking an
// fulla::common::ports::ICryptoProvider& instead of hardcoding a
// static fulla::drogon::adapters::OpenSslCryptoProvider instance -- that class is
// Adapter-layer and this package must not depend on it (design.md §4.1
// rule 1), matching the same pattern already established by
// oauth2::pkce::computeCodeChallenge and
// fulla::oauth2::protocol::generateSecureToken/hashToken.
//
// generateCode()/verifyCode() additionally take the current time as an
// explicit Unix-seconds parameter (fulla::common::ports::IClock is the
// project's injectable time seam, design.md §5.6) instead of calling
// std::chrono::system_clock::now() internally, so this module is
// deterministically testable with a fixed time value.

#include <fulla/common/ports/ICryptoProvider.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fulla::identity
{

namespace totp
{

/**
 * @brief Generate a random TOTP secret (base32 encoded, 20 bytes / 160 bits).
 */
std::string generateSecret(fulla::common::ports::ICryptoProvider &crypto);

/**
 * @brief Generate the TOTP code for the given base32 secret at `nowSeconds`.
 * @param secret Base32 encoded secret.
 * @param nowSeconds Current time as Unix epoch seconds.
 * @return 6-digit TOTP code string, or "" if `secret` is not valid base32.
 */
std::string generateCode(const std::string &secret, int64_t nowSeconds);

/**
 * @brief Verify a TOTP code, allowing +/- 1 time step (30s) for clock skew.
 * @param secret Base32 encoded secret.
 * @param code 6-digit code to verify.
 * @param nowSeconds Current time as Unix epoch seconds.
 * @return true iff `code` matches for time step -1, 0, or +1.
 */
bool verifyCode(const std::string &secret, const std::string &code, int64_t nowSeconds);

/**
 * @brief Build an otpauth:// URI for QR code scanning.
 * @param secret Base32 encoded secret.
 * @param accountName User identifier (email or username).
 * @param issuer Service name shown in the authenticator app.
 */
std::string generateOtpAuthUri(
  const std::string &secret,
  const std::string &accountName,
  const std::string &issuer = "OAuth2Server"
);

/**
 * @brief Generate one-time-use backup codes.
 * @param crypto Crypto provider used for secure randomness.
 * @param count Number of codes to generate (default 10).
 * @return Vector of 8-character alphanumeric codes (charset excludes
 * ambiguous I/O/0/1).
 */
std::vector<std::string> generateBackupCodes(
  fulla::common::ports::ICryptoProvider &crypto,
  int count = 10
);

}  // namespace totp

}  // namespace fulla::identity
