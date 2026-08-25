#pragma once

// Task 15 (fulla-sdk-refactor, design.md §6/§8): fake implementations
// of common::ports interfaces. See FakeClock.h's header comment for the
// placement rationale (dedicated fulla-common-testing library,
// separate from both libs/common itself and OAuth2Plugin's production
// Adapter implementations).
//
// FakeCryptoProvider: a DETERMINISTIC ICryptoProvider for Domain unit
// tests. Only `secureRandomBytes()` behaves differently from the
// production OpenSslCryptoProvider: real cryptographic hashing/HMAC/PBKDF2/
// base64url/RSA-signing all remain REAL OpenSSL operations (there is no
// "fake" version of SHA-256 that would be useful to a test -- these
// primitives are already pure, deterministic functions of their input, so
// faking them would only make tests assert against reimplemented-by-hand
// expected values instead of the real algorithm). What genuinely needs to
// be controllable for a reproducible test is the ONE source of
// non-determinism this port exposes: secureRandomBytes(). This class
// replaces the CSPRNG with a seeded, reproducible byte sequence so a test
// can assert on exact generated-token/salt values and rerun deterministically.
//
// Deliberately self-contained (does not include or depend on
// OAuth2Plugin/include/oauth2/adapters/OpenSslCryptoProvider.h): that class
// lives in OAuth2Plugin, which depends on libs/common, not the other way
// around -- this test-support library must stay usable by future
// standalone Domain packages (libs/oauth2, libs/identity, M2b/M2.5) without
// ever pulling in OAuth2Plugin. The small amount of duplicated OpenSSL
// plumbing (SHA-256/HMAC/PBKDF2/base64url/RSA-sign) is the accepted cost of
// keeping the dependency direction correct.

#include <fulla/common/ports/ICryptoProvider.h>

#include <cstdint>

namespace fulla::common::testing
{

class FakeCryptoProvider : public fulla::common::ports::ICryptoProvider
{
  public:
    FakeCryptoProvider() : FakeCryptoProvider(0)
    {
    }

    /// Construct with a specific seed for the deterministic
    /// secureRandomBytes() byte sequence (default seed: 0).
    explicit FakeCryptoProvider(uint64_t seed) : seed_(seed), state_(seed)
    {
    }

    std::vector<unsigned char> sha256(const std::string &data) override;
    std::string sha256Hex(const std::string &data) override;

    /// Deterministic pseudo-random bytes derived from an internal seeded
    /// counter (xorshift64*-based), NOT a real CSPRNG. Always returns true
    /// (this fake has no "CSPRNG failure" mode to simulate). Two
    /// FakeCryptoProvider instances constructed with the same seed produce
    /// the same byte sequence, in the same call order.
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

    /// Reset the deterministic byte sequence back to the start (as if
    /// freshly constructed with the current seed).
    void reset()
    {
        state_ = seed_;
    }

  private:
    uint64_t seed_ = 0;
    uint64_t state_ = 0;

    /// Draw the next 64 pseudo-random bits from the xorshift64* generator,
    /// advancing state_.
    uint64_t nextRandom64();
};

}  // namespace fulla::common::testing
