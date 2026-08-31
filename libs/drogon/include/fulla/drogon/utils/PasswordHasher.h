#pragma once

#include <string>

namespace fulla::common::utils
{
/**
 * @brief Password hashing utility using PBKDF2-SHA256 (via OpenSSL).
 *
 * New passwords are hashed with PBKDF2-SHA256 (310,000 iterations, OWASP 2023).
 * Legacy SHA-256+salt verification is RETIRED (#103): hashes without the
 * "$pbkdf2-sha256$" prefix fail verification outright. Users still holding
 * legacy hashes migrate via password reset (which rewrites PBKDF2) or an
 * explicitly reopened auth.allow_legacy_hash window on the identity login
 * path — see docs/operate/configuration-guide.md.
 *
 * Storage format: $pbkdf2-sha256$<iterations>$<hex-salt>$<hex-hash>
 *
 * No external dependencies beyond OpenSSL (already linked via Drogon).
 * Can be upgraded to Argon2id in the future by adding libsodium.
 */
class PasswordHasher
{
  public:
    /**
     * @brief Hash a password using PBKDF2-SHA256
     * @param password The plaintext password
     * @return PBKDF2 hash string with embedded salt and parameters
     *         Format: $pbkdf2-sha256$310000$<hex-salt>$<hex-hash>
     */
    static std::string hash(const std::string &password);

    /**
     * @brief Verify a password against a stored hash
     * - If storedHash starts with "$pbkdf2-sha256$" -> PBKDF2 verification
     * - Otherwise -> false (legacy SHA-256 verification is retired, #103;
     *   legacy hashes are rejected without parsing)
     *
     * @param password The plaintext password to verify
     * @param storedHash The stored hash (must be PBKDF2 format)
     * @param salt Unused (kept for call-site compatibility; PBKDF2 embeds
     *        the salt in the hash string)
     * @return true if password matches
     */
    static bool verify(
      const std::string &password,
      const std::string &storedHash,
      const std::string &salt = ""
    );

    /**
     * @brief Check if a stored hash needs to be upgraded to PBKDF2
     * @param storedHash The stored hash string
     * @return true if hash is in legacy format and should be rehashed
     */
    static bool needsRehash(const std::string &storedHash);
};

}  // namespace fulla::common::utils
