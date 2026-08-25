#pragma once

#include <string>

namespace fulla::common::utils
{
/**
 * @brief Password hashing utility using PBKDF2-SHA256 (via OpenSSL)
 * with backward compatibility for legacy SHA-256+salt hashes.
 *
 * New passwords are hashed with PBKDF2-SHA256 (310,000 iterations, OWASP 2023).
 * Legacy SHA-256+salt hashes are detected and verified for migration.
 * After successful legacy verification, callers should rehash with PBKDF2.
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
     * Automatically detects format:
     * - If storedHash starts with "$pbkdf2-sha256$" -> PBKDF2 verification
     * - Otherwise -> Legacy SHA-256+salt verification
     *
     * @param password The plaintext password to verify
     * @param storedHash The stored hash (PBKDF2 or legacy hex)
     * @param salt The salt (only used for legacy SHA-256 verification)
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
