#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>
#include <fulla/drogon/utils/PasswordHasher.h>

using namespace fulla::common::utils;

DROGON_TEST(Unit_P2_PasswordHasher_HashFormat)
{
    auto hash = PasswordHasher::hash("testpassword");
    CHECK(hash.find("$pbkdf2-sha256$") == 0);
    CHECK(hash.length() > 50);
}

DROGON_TEST(Unit_P2_PasswordHasher_VerifyCorrect)
{
    auto hash = PasswordHasher::hash("mypassword");
    CHECK(PasswordHasher::verify("mypassword", hash) == true);
}

DROGON_TEST(Unit_P2_PasswordHasher_VerifyWrong)
{
    auto hash = PasswordHasher::hash("mypassword");
    CHECK(PasswordHasher::verify("wrongpassword", hash) == false);
}

DROGON_TEST(Unit_P2_PasswordHasher_LegacyVerify)
{
    // Legacy SHA-256+salt format
    std::string salt = "test-salt";
    std::string password = "admin";
    std::string legacyHash = ::drogon::utils::getSha256(password + salt);
    CHECK(PasswordHasher::verify(password, legacyHash, salt) == true);
    CHECK(PasswordHasher::verify("wrong", legacyHash, salt) == false);
}

DROGON_TEST(Unit_P2_PasswordHasher_NeedsRehash)
{
    auto pbkdf2Hash = PasswordHasher::hash("test");
    CHECK(PasswordHasher::needsRehash(pbkdf2Hash) == false);
    CHECK(PasswordHasher::needsRehash("abcdef1234567890") == true);
}

// ---------------------------------------------------------------------------
// Coverage additions (P1): malformed-hash rejection branches in verify()
// (PasswordHasher.cc:100, 118, 146). These defend against accepting
// truncated/corrupt/prefix-less stored hashes -- security-relevant and
// previously untested.
// ---------------------------------------------------------------------------

DROGON_TEST(Unit_P1_PasswordHasher_VerifyMalformedHash_NoPrefix_ReturnsFalse)
{
    // A hash without the "$pbkdf2-sha256$" prefix takes the legacy branch
    // (needsRehash==true) but fails the length/case compare -> false.
    CHECK(PasswordHasher::verify("anypw", "not-a-real-hash", "salt") == false);
}

DROGON_TEST(Unit_P1_PasswordHasher_VerifyMalformedHash_WrongPartCount_ReturnsFalse)
{
    // Has the prefix but only 3 '$'-parts (expected 4) -> parts.size()!=4.
    CHECK(PasswordHasher::verify("anypw", "$pbkdf2-sha256$310000$abc") == false);
    // 5 parts is also rejected.
    CHECK(PasswordHasher::verify("anypw", "$pbkdf2-sha256$310000$ab$cd$ef") == false);
}

DROGON_TEST(Unit_P1_PasswordHasher_VerifyMalformedHash_EmptyHash_ReturnsFalse)
{
    // Empty stored hash -> no prefix match on the PBKDF2 path; the legacy
    // path's length check (0 != sha256 output length) also fails.
    CHECK(PasswordHasher::verify("anypw", "") == false);
}

DROGON_TEST(Unit_P1_PasswordHasher_VerifyMalformedHash_ShortHashLengthMismatch_ReturnsFalse)
{
    // PBKDF2 prefix + 4 parts, but the hex-hash segment is too short for
    // the constant-time compare length guard (PasswordHasher.cc:146-149).
    // Hex salt padded to look parseable but hash segment is 1 hex char.
    CHECK(
      PasswordHasher::verify("anypw", "$pbkdf2-sha256$310000$00112233445566778899aabbccddeeff$0") ==
      false
    );
}

DROGON_TEST(Unit_P1_PasswordHasher_LegacyVerify_LengthMismatch_ReturnsFalse)
{
    // A legacy hash whose length differs from the computed SHA-256 output
    // (64 hex chars) -> false (PasswordHasher.cc:171-174).
    std::string salt = "salt";
    std::string password = "pw";
    // 32-char legacy hash (half the real 64-char SHA-256 hex output).
    std::string shortLegacy(32, 'a');
    CHECK(PasswordHasher::verify(password, shortLegacy, salt) == false);
}
