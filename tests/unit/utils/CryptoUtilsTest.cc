// Coverage additions (P1, fulla coverage push): CryptoUtils.h's PKCE
// helpers (computeCodeChallenge / isValidCodeVerifier / isValidCodeChallenge)
// and hashToken had no direct unit coverage -- they were exercised only
// indirectly through the OAuth2Plugin-side TokenService/Controller tests.
// These pin the helper-level contracts: the plain-method identity branch,
// S256 base64url-of-digest, unknown-method fallback, the 43/128 length
// boundaries for both verifier and challenge, the charset check, the
// hashToken uppercase-hex / 64-char contract, and generateSecureToken's
// length/non-emptiness.

#include <drogon/drogon_test.h>
#include <fulla/drogon/utils/CryptoUtils.h>

#include <algorithm>
#include <string>

using namespace fulla::drogon::utils;

namespace
{
bool isBase64UrlChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_';
}
bool isUpperHexChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}
}  // namespace

// --- computeCodeChallenge ---

DROGON_TEST(Unit_P2_CryptoUtils_ComputeCodeChallenge_Plain_ReturnsVerifier)
{
    const std::string verifier(43, 'a');
    CHECK(computeCodeChallenge(verifier, "plain") == verifier);
}

DROGON_TEST(Unit_P2_CryptoUtils_ComputeCodeChallenge_S256_IsBase64UrlOfDigest)
{
    const std::string verifier(43, 'a');
    auto challenge = computeCodeChallenge(verifier, "S256");
    // SHA-256 digest = 32 bytes -> base64url (no padding) = 43 chars.
    CHECK(challenge.length() == 43);
    for (char c : challenge)
        CHECK(isBase64UrlChar(c));
    // plain must NOT equal S256 for a non-trivial verifier.
    CHECK(challenge != verifier);
}

DROGON_TEST(Unit_P2_CryptoUtils_ComputeCodeChallenge_UnknownMethod_FallsBackToVerifier)
{
    const std::string verifier(43, 'b');
    // Any method that is not exactly "S256" takes the else branch (plain).
    CHECK(computeCodeChallenge(verifier, "S999") == verifier);
    CHECK(computeCodeChallenge(verifier, "") == verifier);
}

// --- isValidCodeVerifier / isValidCodeChallenge ---

DROGON_TEST(Unit_P2_CryptoUtils_IsValidCodeVerifier_Boundaries_43And128_Accepted)
{
    CHECK(isValidCodeVerifier(std::string(43, 'a')) == true);
    CHECK(isValidCodeVerifier(std::string(128, 'a')) == true);
}

DROGON_TEST(Unit_P2_CryptoUtils_IsValidCodeVerifier_TooShortAndTooLong_Rejected)
{
    CHECK(isValidCodeVerifier(std::string(42, 'a')) == false);
    CHECK(isValidCodeVerifier(std::string(129, 'a')) == false);
}

DROGON_TEST(Unit_P2_CryptoUtils_IsValidCodeVerifier_InvalidCharset_Rejected)
{
    CHECK(isValidCodeVerifier(std::string(43, '+')) == false);   // '+' not allowed
    CHECK(isValidCodeVerifier(std::string(43, ' ')) == false);   // space not allowed
}

DROGON_TEST(Unit_P2_CryptoUtils_IsValidCodeVerifier_LegalSpecials_Accepted)
{
    // The four legal unreserved specials [-._~] are accepted at boundary length.
    std::string v(43, '-');
    CHECK(isValidCodeVerifier(v) == true);
    v = std::string(43, '.');
    CHECK(isValidCodeVerifier(v) == true);
    v = std::string(43, '_');
    CHECK(isValidCodeVerifier(v) == true);
    v = std::string(43, '~');
    CHECK(isValidCodeVerifier(v) == true);
}

DROGON_TEST(Unit_P2_CryptoUtils_IsValidCodeChallenge_Boundaries_43And128_Accepted)
{
    CHECK(isValidCodeChallenge(std::string(43, 'a')) == true);
    CHECK(isValidCodeChallenge(std::string(128, 'a')) == true);
}

DROGON_TEST(Unit_P2_CryptoUtils_IsValidCodeChallenge_TooShortAndTooLong_Rejected)
{
    CHECK(isValidCodeChallenge(std::string(42, 'a')) == false);
    CHECK(isValidCodeChallenge(std::string(129, 'a')) == false);
}

DROGON_TEST(Unit_P2_CryptoUtils_IsValidCodeChallenge_InvalidCharset_Rejected)
{
    CHECK(isValidCodeChallenge(std::string(43, '/')) == false);
}

// --- hashToken ---

DROGON_TEST(Unit_P2_CryptoUtils_HashToken_ReturnsUppercaseHexOfCorrectLength)
{
    auto h = hashToken("any-token-value");
    CHECK(h.length() == 64);
    for (char c : h)
        CHECK(isUpperHexChar(c));
}

DROGON_TEST(Unit_P2_CryptoUtils_HashToken_DeterministicAndDifferentInputs)
{
    CHECK(hashToken("same") == hashToken("same"));
    CHECK(hashToken("a") != hashToken("b"));
}

// --- generateSecureToken ---

DROGON_TEST(Unit_P2_CryptoUtils_GenerateSecureToken_DefaultLengthAndUniqueness)
{
    auto t1 = generateSecureToken();
    auto t2 = generateSecureToken();
    // Default 32 bytes -> base64url 43 chars, no padding, no + or /.
    CHECK(t1.length() == 43);
    CHECK(t1.find('+') == std::string::npos);
    CHECK(t1.find('/') == std::string::npos);
    CHECK(t1.find('=') == std::string::npos);
    // Two draws differ (CSPRNG).
    CHECK(t1 != t2);
}

DROGON_TEST(Unit_P2_CryptoUtils_GenerateSecureToken_CustomByteCount)
{
    // 16 bytes -> ceil(16/3)*4 minus padding = 22 chars.
    auto t = generateSecureToken(16);
    CHECK(t.length() == 22);
}
