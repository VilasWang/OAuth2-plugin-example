// Task 17 (authforge-sdk-refactor, design.md §8/§17): unit tests for the
// oauth2::pkce pure-function module. Uses authforge::common::testing's
// FakeCryptoProvider for the S256 case, which performs REAL SHA-256/
// base64url under the hood (see that class's header comment) -- so these
// assertions exercise the actual RFC 7636 algorithm, not a stub.

#include <authforge/common/model/PkceChallenge.h>
#include <authforge/common/testing/FakeCryptoProvider.h>
#include <authforge/oauth2/pkce/Pkce.h>

#include <gtest/gtest.h>

namespace
{

using authforge::common::model::PkceChallenge;
using authforge::common::testing::FakeCryptoProvider;
using namespace authforge::oauth2::pkce;

// A 43-character verifier satisfying RFC 7636 §4.1's charset/length rule.
constexpr char kValidVerifier[] = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";

TEST(PkceTest, ComputeCodeChallenge_S256_MatchesKnownRfc7636Example)
{
    // This is the exact code_verifier/code_challenge pair from RFC 7636
    // Appendix B.
    FakeCryptoProvider crypto;
    const std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    const std::string expectedChallenge = "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM";

    EXPECT_EQ(computeCodeChallenge(verifier, "S256", crypto), expectedChallenge);
}

TEST(PkceTest, ComputeCodeChallenge_Plain_ReturnsVerifierUnchanged)
{
    FakeCryptoProvider crypto;
    EXPECT_EQ(computeCodeChallenge(kValidVerifier, "plain", crypto), kValidVerifier);
}

TEST(PkceTest, ComputeCodeChallenge_UnknownMethod_FallsBackToIdentity)
{
    FakeCryptoProvider crypto;
    EXPECT_EQ(computeCodeChallenge(kValidVerifier, "bogus", crypto), kValidVerifier);
}

TEST(PkceTest, VerifyCodeVerifier_S256_CorrectVerifier_Succeeds)
{
    FakeCryptoProvider crypto;
    const std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    PkceChallenge challenge("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", "S256");

    EXPECT_TRUE(verifyCodeVerifier(verifier, challenge, crypto));
}

TEST(PkceTest, VerifyCodeVerifier_S256_WrongVerifier_Fails)
{
    FakeCryptoProvider crypto;
    // Deliberately a DIFFERENT valid-format verifier than the one that
    // actually produced this challenge (see the RFC 7636 test above), so
    // this asserts a genuine mismatch rather than accidentally reusing
    // the matching verifier under a different variable name.
    const std::string wrongVerifier = "aBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXm";
    PkceChallenge challenge("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", "S256");

    EXPECT_FALSE(verifyCodeVerifier(wrongVerifier, challenge, crypto));
}

TEST(PkceTest, VerifyCodeVerifier_Plain_MatchingValues_Succeeds)
{
    FakeCryptoProvider crypto;
    PkceChallenge challenge(kValidVerifier, "plain");

    EXPECT_TRUE(verifyCodeVerifier(kValidVerifier, challenge, crypto));
}

TEST(PkceTest, IsValidCodeVerifierFormat_ValidValue_ReturnsTrue)
{
    EXPECT_TRUE(isValidCodeVerifierFormat(kValidVerifier));
}

TEST(PkceTest, IsValidCodeVerifierFormat_TooShort_ReturnsFalse)
{
    EXPECT_FALSE(isValidCodeVerifierFormat("short"));
}

TEST(PkceTest, IsValidCodeVerifierFormat_TooLong_ReturnsFalse)
{
    EXPECT_FALSE(isValidCodeVerifierFormat(std::string(129, 'a')));
}

TEST(PkceTest, IsValidCodeVerifierFormat_InvalidCharset_ReturnsFalse)
{
    // 43 chars, but contains '+' which is not in the RFC 7636 unreserved set.
    EXPECT_FALSE(isValidCodeVerifierFormat("dBjftJeZ4CVP+mB92K27uhbUJU1p1r_wW1gFWFOEjXk"));
}

TEST(PkceTest, IsValidCodeVerifierFormat_BoundaryLengths_43And128_ReturnTrue)
{
    EXPECT_TRUE(isValidCodeVerifierFormat(std::string(43, 'a')));
    EXPECT_TRUE(isValidCodeVerifierFormat(std::string(128, 'a')));
}

TEST(PkceTest, IsValidCodeChallengeFormat_ValidValue_ReturnsTrue)
{
    EXPECT_TRUE(isValidCodeChallengeFormat("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
}

TEST(PkceTest, IsValidCodeChallengeFormat_InvalidCharset_ReturnsFalse)
{
    EXPECT_FALSE(isValidCodeChallengeFormat("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw/cM+padding"));
}

// ---------------------------------------------------------------------------
// Coverage additions (P2/P3): plain-mismatch / unknown-method-mismatch
// negative cases, acceptance of every legal unreserved special char, and
// the code_challenge length boundaries (previously only verifier was tested
// at the boundaries).
// ---------------------------------------------------------------------------

// verifyCodeVerifier: plain method with a MISMATCHING verifier fails.
TEST(PkceTest, VerifyCodeVerifier_Plain_Mismatch_Fails)
{
    FakeCryptoProvider crypto;
    PkceChallenge challenge(kValidVerifier, "plain");
    EXPECT_FALSE(verifyCodeVerifier("a-different-verifier-of-suitable-length-here", challenge, crypto));
}

// Note: verifyCodeVerifier with an UNKNOWN method is NOT testable here --
// PkceChallenge's constructor validates the method at construction time
// (only "S256"/"plain" accepted), so a challenge carrying any other method
// cannot exist to verify against. The computeCodeChallenge unknown-method
// fallback path is covered by ComputeCodeChallenge_UnknownMethod_FallsBackToIdentity above.

// isValidCodeVerifierFormat: every one of the four legal unreserved
// specials [-._~] is accepted (the existing charset test only proved '+'
// is rejected).
TEST(PkceTest, IsValidCodeVerifierFormat_AllLegalSpecials_ReturnsTrue)
{
    EXPECT_TRUE(isValidCodeVerifierFormat(std::string(43, '-')));
    EXPECT_TRUE(isValidCodeVerifierFormat(std::string(43, '.')));
    EXPECT_TRUE(isValidCodeVerifierFormat(std::string(43, '_')));
    EXPECT_TRUE(isValidCodeVerifierFormat(std::string(43, '~')));
}

// isValidCodeChallengeFormat: length boundaries (43 and 128) accepted.
TEST(PkceTest, IsValidCodeChallengeFormat_BoundaryLengths_43And128_ReturnTrue)
{
    EXPECT_TRUE(isValidCodeChallengeFormat(std::string(43, 'a')));
    EXPECT_TRUE(isValidCodeChallengeFormat(std::string(128, 'a')));
}

// isValidCodeChallengeFormat: too short (42) and too long (129) rejected.
TEST(PkceTest, IsValidCodeChallengeFormat_TooShortAndTooLong_ReturnFalse)
{
    EXPECT_FALSE(isValidCodeChallengeFormat(std::string(42, 'a')));
    EXPECT_FALSE(isValidCodeChallengeFormat(std::string(129, 'a')));
}

}  // namespace
