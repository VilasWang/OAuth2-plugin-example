#pragma once

#include <cctype>
#include <string>
#include <vector>

// Task 14 (authforge-sdk-refactor, design.md §5.6): this header no longer
// calls drogon::utils::* directly. Every function below now delegates to
// authforge::drogon::adapters::OpenSslCryptoProvider (the Adapter-side default
// implementation of authforge::common::ports::ICryptoProvider added in
// Task 14 slice 1), which is pure OpenSSL with zero Drogon dependency.
//
// Public API is UNCHANGED (same free-function names/signatures/semantics)
// so all 14 existing #include sites of this header (TokenService,
// JwkManager, and 9 Server-side controllers) keep working without any
// call-site edit -- this slice's entire point is removing the
// drogon::utils dependency from the SHARED IMPLEMENTATION, not migrating
// every caller to inject ICryptoProvider individually (that finer-grained
// DI step, if ever needed for testability, is separate follow-up work
// design.md does not currently ask for).
//
// OpenSslCryptoProvider is stateless (every method allocates its own
// OpenSSL context per call, see its own header's thread-safety note), so a
// single static instance shared by every inline function below is safe to
// call concurrently from many request threads.
#include <authforge/drogon/adapters/OpenSslCryptoProvider.h>
#include <authforge/drogon/adapters/OpenSslUuidGenerator.h>

namespace authforge::drogon::utils
{

namespace detail
{
// Shared, stateless OpenSslCryptoProvider instance backing every function
// in this header. See file header comment for the thread-safety rationale.
inline authforge::drogon::adapters::OpenSslCryptoProvider &cryptoProvider()
{
    static authforge::drogon::adapters::OpenSslCryptoProvider instance;
    return instance;
}
}  // namespace detail

/**
 * @brief Base64URL encode data (RFC 4648 Section 5)
 *
 * This replaces '+' with '-' and '/' with '_' for URL-safe encoding, with
 * no padding.
 *
 * @param data Data to encode
 * @return Base64URL encoded string (without padding)
 */
inline std::string base64UrlEncode(const std::string &data)
{
    return detail::cryptoProvider().base64UrlEncode(data);
}

/**
 * @brief Base64URL encode binary data
 *
 * @param bytes Binary data to encode
 * @param length Length of binary data
 * @return Base64URL encoded string (without padding)
 */
inline std::string base64UrlEncode(const unsigned char *bytes, size_t length)
{
    return detail::cryptoProvider().base64UrlEncode(bytes, length);
}

/**
 * @brief Compute SHA-256 hash (RFC 7636 for PKCE)
 *
 * @param data Input data
 * @return SHA-256 hash as vector of unsigned chars (32 bytes)
 */
inline std::vector<unsigned char> sha256(const std::string &data)
{
    return detail::cryptoProvider().sha256(data);
}

/**
 * @brief Compute PKCE code challenge from verifier (RFC 7636)
 *
 * For method "S256": code_challenge = BASE64URL(SHA256(ASCII(code_verifier)))
 * For method "plain": code_challenge = code_verifier
 *
 * @param codeVerifier The code verifier (43-128 characters)
 * @param method Challenge method ("plain" or "S256")
 * @return The code challenge
 */
inline std::string computeCodeChallenge(const std::string &codeVerifier, const std::string &method)
{
    if (method == "S256")
    {
        // SHA-256(code_verifier) -> base64url
        auto hash = sha256(codeVerifier);
        return base64UrlEncode(hash.data(), hash.size());
    }
    else
    {
        // plain method
        return codeVerifier;
    }
}

/**
 * @brief Validate code verifier format (RFC 7636)
 *
 * Code verifier must be 43-128 characters of [A-Za-z0-9-._~]
 *
 * @param codeVerifier The code verifier to validate
 * @return true if valid, false otherwise
 */
inline bool isValidCodeVerifier(const std::string &codeVerifier)
{
    if (codeVerifier.length() < 43 || codeVerifier.length() > 128)
    {
        return false;
    }

    // Check character set: [A-Za-z0-9-._~]
    for (char c : codeVerifier)
    {
        if (
          !std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '.' && c != '_' &&
          c != '~'
        )
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Validate code challenge format (RFC 7636)
 *
 * Code challenge must be 43-128 characters of [A-Za-z0-9-._~]
 *
 * @param codeChallenge The code challenge to validate
 * @return true if valid, false otherwise
 */
inline bool isValidCodeChallenge(const std::string &codeChallenge)
{
    if (codeChallenge.length() < 43 || codeChallenge.length() > 128)
    {
        return false;
    }

    // Check character set: [A-Za-z0-9-._~]
    for (char c : codeChallenge)
    {
        if (
          !std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '.' && c != '_' &&
          c != '~'
        )
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Generate a cryptographically secure random token
 *
 * Generates high-entropy random data, then encodes it as base64url (no
 * padding). Default 32 bytes = 256 bits of entropy, producing a
 * 43-character string.
 *
 * @param bytes Number of random bytes (default 32 = 256 bits)
 * @return Base64URL encoded random token string
 */
inline std::string generateSecureToken(size_t bytes = 32)
{
    std::vector<unsigned char> buffer(bytes);
    if (!detail::cryptoProvider().secureRandomBytes(buffer.data(), bytes))
    {
        // Fallback if secure random fails (should never happen). Uses two
        // freshly generated UUIDs concatenated, matching the pre-migration
        // fallback shape exactly (same entropy source underneath -- see
        // OpenSslUuidGenerator, itself RAND_bytes-backed -- so this remains
        // "should never happen" defense-in-depth, not a weaker substitute).
        authforge::drogon::adapters::OpenSslUuidGenerator uuidGenerator;
        return uuidGenerator.generate() + uuidGenerator.generate();
    }
    return base64UrlEncode(buffer.data(), buffer.size());
}

/**
 * @brief Hash a token for secure storage
 *
 * Computes SHA-256 of the raw token and returns an UPPERCASE hex string
 * (64 chars). Used to store tokens in the database without exposing the
 * raw value, AND as the exact-match lookup key on read
 * (storage_->getAccessToken/getRefreshToken/introspectToken/
 * revokeAccessToken all query `WHERE token = $1`, an exact string
 * comparison, not a case-insensitive one).
 *
 * Case is UPPERCASE, not lowercase, despite this function's historical
 * doc comment claiming otherwise (verified while writing Task 14's
 * OpenSslCryptoProviderTest.cc: the pre-migration implementation
 * delegated to drogon::utils::getSha256(), which actually returns
 * UPPERCASE hex). This is NOT a stylistic choice this migration is free
 * to change: every access/refresh token row already persisted in a
 * production database was hashed with the pre-migration UPPERCASE
 * behavior, and every read path is an EXACT string match against that
 * stored value (unlike e.g. RedisClientRepository::validateClient's
 * secret-hash comparison, which explicitly lowercases both sides before
 * comparing). Switching this function to lowercase would silently break
 * every token lookup against pre-existing data -- so this migration
 * preserves the exact pre-migration case rather than "fixing" it to match
 * the doc comment's stale claim.
 *
 * @param rawToken The raw token string (as returned to the client)
 * @return Uppercase hex SHA-256 hash (64 characters)
 */
inline std::string hashToken(const std::string &rawToken)
{
    std::string hex = detail::cryptoProvider().sha256Hex(rawToken);
    for (char &c : hex)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return hex;
}

}  // namespace authforge::drogon::utils
