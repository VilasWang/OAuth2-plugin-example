#pragma once

// Task 14 (authforge-sdk-refactor, design.md §5.6): Adapter-side default
// implementation of authforge::common::ports::ICryptoProvider, backed
// directly by OpenSSL (design.md: "OpenSSL 直接实现（脱 drogon）").
//
// Placement note: this lives under OAuth2Plugin/include/oauth2/adapters/
// rather than a not-yet-created libs/drogon or libs/storage-* package.
// Per design.md's target layout (§6), Adapter implementations of common
// ports eventually belong in an Adapter package, but libs/drogon does not
// exist yet (that is M3, Task 20) and this implementation itself has NO
// Drogon dependency (pure OpenSSL) -- it does not need to wait for
// libs/drogon to exist. Keeping it inside OAuth2Plugin for now (which
// already links OpenSSL::Crypto, see OAuth2Plugin/CMakeLists.txt) avoids
// inventing a new top-level package for a single small adapter class ahead
// of the M3 milestone that actually reorganizes Adapter code; a later
// milestone's directory move (Task 39) is expected to relocate this file
// verbatim.
//
// This class is stateless and thread-safe (every method call creates its
// own OpenSSL context, mirroring the concurrency contract JwkManager::signJwt
// already documents for its own per-call EVP_MD_CTX use).

#include <authforge/common/ports/ICryptoProvider.h>

namespace oauth2::adapters
{

class OpenSslCryptoProvider : public authforge::common::ports::ICryptoProvider
{
  public:
    std::vector<unsigned char> sha256(const std::string &data) override;
    std::string sha256Hex(const std::string &data) override;
    bool secureRandomBytes(unsigned char *buffer, size_t length) override;
    std::string base64UrlEncode(const unsigned char *bytes, size_t length) override;
    std::string base64UrlEncode(const std::string &data) override;
    std::vector<unsigned char> base64UrlDecode(const std::string &encoded) override;
    std::vector<unsigned char> hmacSha256(const std::string &key, const std::string &data) override;
    std::vector<unsigned char> pbkdf2HmacSha256(
      const std::string &password,
      const std::string &salt,
      int iterations,
      size_t keyLength
    ) override;
    std::vector<unsigned char> rsaSign(
      const std::string &privateKeyPem,
      const std::string &digestAlgorithm,
      const std::string &data
    ) override;
};

}  // namespace oauth2::adapters
