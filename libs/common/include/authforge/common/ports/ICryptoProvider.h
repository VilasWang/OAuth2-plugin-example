#pragma once

// Task 13 (authforge-sdk-refactor, design.md §5.6/§6): libs/common ports.
//
// ICryptoProvider is the single largest drogon::utils replacement point
// design.md §5.6 identifies ("Domain 去 Drogon 化的主体工作不是 OpenSSL...
// 而是替换散落各处的 drogon::utils"). It covers every cryptographic
// primitive the Domain layer currently reaches for via
// authforge::common::utils::CryptoUtils.h (a header-only file wrapping
// drogon::utils::base64EncodeUnpadded / getSha256 / secureRandomBytes /
// getUuid) plus the additional primitives design.md's port table lists
// that CryptoUtils.h does not yet cover (HMAC, PBKDF2, RSA/JWT signing --
// currently implemented ad hoc inside JwkManager/TokenService/
// PasswordHasher/TotpUtils per design.md §5.6's audit list).
//
// Scope note: this task (13) only DECLARES the port. The default
// (Adapter-side, OpenSSL-direct) implementation and the migration of every
// existing call site (CryptoUtils.h's 14 include-sites, PasswordHasher,
// TotpUtils, JwkManager, TokenService, AuditLogger, RequestId) is Task 14
// ("去 Drogon::utils：实现并替换所有调用点"), deliberately scoped separately
// per design.md's own guidance: "按「每类端口一个 PR、单 PR 调用点数设
// 上限、超限即停并汇报进度」控制，勿一次性大爆炸." Declaring the full port
// shape now (rather than growing it incrementally per call site) lets
// Task 14 proceed call-site-by-call-site against a stable interface
// instead of repeatedly extending this header out from under in-progress
// migration work.
//
// Method shapes are modeled directly on CryptoUtils.h's existing free
// functions (base64UrlEncode/sha256/generateSecureToken/hashToken) so the
// eventual Task 14 migration is closer to a call-site rename than a
// redesign, plus the HMAC/PBKDF2/RSA-sign primitives design.md's table
// calls out that do not have a CryptoUtils.h precedent yet.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace authforge::common::ports
{

/**
 * @brief Cryptographic primitives needed by the Domain layer, injected so
 * Domain code never calls drogon::utils::* or OpenSSL directly (design.md
 * §5.6). The default production implementation is Adapter-side and backed
 * directly by OpenSSL (design.md: "OpenSSL 直接实现（脱 drogon）").
 */
class ICryptoProvider
{
  public:
    virtual ~ICryptoProvider() = default;

    /// SHA-256 hash of `data`, returned as raw bytes (32 bytes).
    virtual std::vector<unsigned char> sha256(const std::string &data) = 0;

    /// SHA-256 hash of `data`, returned as a lowercase hex string (64 chars).
    /// Mirrors CryptoUtils.h's hashToken()/drogon::utils::getSha256() shape,
    /// used for token-at-rest hashing and similar hex-digest use cases.
    virtual std::string sha256Hex(const std::string &data) = 0;

    /// Fill `buffer` with `length` cryptographically secure random bytes.
    /// Returns true on success, false if the underlying CSPRNG call
    /// failed (mirrors drogon::utils::secureRandomBytes's bool return).
    virtual bool secureRandomBytes(unsigned char *buffer, size_t length) = 0;

    /// Base64url-encode (RFC 4648 §5, unpadded) a raw byte buffer.
    virtual std::string base64UrlEncode(const unsigned char *bytes, size_t length) = 0;

    /// @overload for a std::string input.
    virtual std::string base64UrlEncode(const std::string &data) = 0;

    /// Base64url-decode (RFC 4648 §5, unpadded) into raw bytes. Returns an
    /// empty vector if `encoded` is not valid base64url.
    virtual std::vector<unsigned char> base64UrlDecode(const std::string &encoded) = 0;

    /// HMAC-SHA256(key, data), returned as raw bytes (32 bytes). Used by
    /// JWS HS256 signing (JwkManager) and similar MAC use cases.
    virtual std::vector<unsigned char> hmacSha256(
      const std::string &key,
      const std::string &data
    ) = 0;

    /// PBKDF2-HMAC-SHA256(password, salt, iterations, keyLength), returned
    /// as raw derived-key bytes. Used by PasswordHasher.
    virtual std::vector<unsigned char> pbkdf2HmacSha256(
      const std::string &password,
      const std::string &salt,
      int iterations,
      size_t keyLength
    ) = 0;

    /// Sign `data` with an RSA private key (PEM-encoded) using RSASSA-PKCS1-v1_5
    /// with the given digest algorithm name (e.g. "SHA256", matching JWS
    /// RS256). Returns the raw signature bytes, or an empty vector on
    /// failure (invalid key, unsupported digest, etc). Used by JwkManager
    /// for RS256 JWT signing.
    virtual std::vector<unsigned char> rsaSign(
      const std::string &privateKeyPem,
      const std::string &digestAlgorithm,
      const std::string &data
    ) = 0;
};

}  // namespace authforge::common::ports
