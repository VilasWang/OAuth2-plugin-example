// Coverage gap (fulla-sdk-refactor, design.md §6/§8 "protocol/"):
// TokenCrypto.cc had ZERO direct unit coverage -- both functions were
// exercised only transitively via TokenServiceTest. These tests pin the
// Domain-layer token-crypto contract directly: generateSecureToken's
// length/non-emptiness/determinism and hashToken's uppercase-hex / length /
// determinism (the uppercasing is load-bearing for at-rest lookups, see
// TokenService.h's "UPPERCASE hex" comment).

#include <fulla/common/testing/FakeCryptoProvider.h>
#include <fulla/oauth2/protocol/TokenCrypto.h>

#include <gtest/gtest.h>

#include <cctype>
#include <string>

namespace
{

using fulla::common::testing::FakeCryptoProvider;
using fulla::oauth2::protocol::generateSecureToken;
using fulla::oauth2::protocol::hashToken;

bool isBase64UrlChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_';
}

bool isUpperHexChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

// Default byte count (32) -> base64url of 32 bytes = ceil(32*4/3) with no
// padding = 43 chars (32 % 3 == 2 -> 43 chars). Asserts the length contract
// and the base64url alphabet (no '+', '/', or '=' padding).
TEST(TokenCryptoTest, GenerateSecureToken_DefaultByteCount_ReturnsNonEmptyBase64Url)
{
    FakeCryptoProvider crypto;
    auto token = generateSecureToken(crypto);
    EXPECT_EQ(token.size(), 43u);
    EXPECT_FALSE(token.empty());
    for (char c : token)
    {
        EXPECT_TRUE(isBase64UrlChar(c)) << "unexpected char in token: " << c;
    }
    EXPECT_EQ(token.find('+'), std::string::npos);
    EXPECT_EQ(token.find('/'), std::string::npos);
    EXPECT_EQ(token.find('='), std::string::npos);
}

// Custom byte count -> length follows base64url(nbytes) without padding.
// 16 bytes -> ceil(16/3)*4 - padding = 22 chars (16 % 3 == 1 -> 22 chars).
TEST(TokenCryptoTest, GenerateSecureToken_CustomByteCount_OutputLengthMatches)
{
    FakeCryptoProvider crypto;
    auto token = generateSecureToken(crypto, 16);
    EXPECT_EQ(token.size(), 22u);
    for (char c : token)
    {
        EXPECT_TRUE(isBase64UrlChar(c));
    }
}

// Empty byte count -> empty string (base64url of zero bytes is the empty
// string). Pins the edge-input behavior.
TEST(TokenCryptoTest, GenerateSecureToken_ZeroBytes_ReturnsEmptyString)
{
    FakeCryptoProvider crypto;
    auto token = generateSecureToken(crypto, 0);
    EXPECT_TRUE(token.empty());
}

// Two tokens from two equally-seeded providers are byte-identical (the
// FakeCryptoProvider's deterministic byte sequence is reproducible).
TEST(TokenCryptoTest, GenerateSecureToken_Deterministic_ForSameSeed)
{
    FakeCryptoProvider a(7);
    FakeCryptoProvider b(7);
    EXPECT_EQ(generateSecureToken(a), generateSecureToken(b));
}

// Different seeds -> different tokens (sanity check on the seeding).
TEST(TokenCryptoTest, GenerateSecureToken_DifferentSeeds_Differ)
{
    FakeCryptoProvider a(1);
    FakeCryptoProvider b(2);
    EXPECT_NE(generateSecureToken(a), generateSecureToken(b));
}

// hashToken returns a 64-char UPPERCASE hex string (SHA-256 hex = 64 chars).
// The uppercasing is intentional and load-bearing -- at-rest token lookups
// hash the presented token the same way and compare against the stored
// (uppercase) hash. A regression here would silently break token validation.
TEST(TokenCryptoTest, HashToken_ReturnsUppercaseHexOfCorrectLength)
{
    FakeCryptoProvider crypto;
    auto h = hashToken(crypto, "some-token-value");
    EXPECT_EQ(h.size(), 64u);
    for (char c : h)
    {
        EXPECT_TRUE(isUpperHexChar(c)) << "non-uppercase-hex char: " << c;
    }
}

// Same input -> same hash (determinism). Required for at-rest equality.
TEST(TokenCryptoTest, HashToken_Deterministic_ForSameInput)
{
    FakeCryptoProvider crypto;
    EXPECT_EQ(hashToken(crypto, "same-input"), hashToken(crypto, "same-input"));
}

// Different inputs -> different hashes.
TEST(TokenCryptoTest, HashToken_DifferentInputs_Differ)
{
    FakeCryptoProvider crypto;
    EXPECT_NE(hashToken(crypto, "input-a"), hashToken(crypto, "input-b"));
}

}  // namespace
