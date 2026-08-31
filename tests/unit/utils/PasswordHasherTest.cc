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

DROGON_TEST(Unit_P2_PasswordHasher_LegacyVerify_Rejected)
{
    // #103: legacy SHA-256+salt verification is RETIRED — even a
    // well-formed legacy hash with the correct password and salt must
    // fail (needsRehash() is the format discriminator; legacy hashes are
    // rejected without parsing).
    std::string salt = "test-salt";
    std::string password = "admin";
    std::string legacyHash = ::drogon::utils::getSha256(password + salt);
    CHECK(PasswordHasher::verify(password, legacyHash, salt) == false);
    CHECK(PasswordHasher::verify("wrong", legacyHash, salt) == false);
}

DROGON_TEST(Unit_P2_PasswordHasher_NeedsRehash)
{
    auto pbkdf2Hash = PasswordHasher::hash("test");
    CHECK(PasswordHasher::needsRehash(pbkdf2Hash) == false);
    CHECK(PasswordHasher::needsRehash("abcdef1234567890") == true);
}

// ---------------------------------------------------------------------------
// Coverage additions (P1): malformed-hash rejection branches in verify().
// These defend against accepting truncated/corrupt/prefix-less stored
// hashes -- security-relevant and previously untested.
// ---------------------------------------------------------------------------

DROGON_TEST(Unit_P1_PasswordHasher_VerifyMalformedHash_NoPrefix_ReturnsFalse)
{
    // #103: a hash without the "$pbkdf2-sha256$" prefix is legacy-format ->
    // rejected outright by the needsRehash() discriminator.
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
    // Empty stored hash -> no prefix -> rejected outright.
    CHECK(PasswordHasher::verify("anypw", "") == false);
}

DROGON_TEST(Unit_P1_PasswordHasher_VerifyMalformedHash_ShortHashLengthMismatch_ReturnsFalse)
{
    // PBKDF2 prefix + 4 parts, but the hex-hash segment is too short for
    // the constant-time compare length guard.
    // Hex salt padded to look parseable but hash segment is 1 hex char.
    CHECK(
      PasswordHasher::verify("anypw", "$pbkdf2-sha256$310000$00112233445566778899aabbccddeeff$0") ==
      false
    );
}

DROGON_TEST(Unit_P1_PasswordHasher_LegacyVerify_AnyLength_Rejected)
{
    // #103: every non-$pbkdf2-sha256$ stored hash is rejected without
    // parsing, whatever its length (the old legacy branch's length
    // arithmetic is gone; the discriminator is prefix-only).
    std::string shortLegacy(32, 'a');
    std::string fullLegacy(64, 'a');
    CHECK(PasswordHasher::verify("pw", shortLegacy, "salt") == false);
    CHECK(PasswordHasher::verify("pw", fullLegacy, "salt") == false);
    CHECK(PasswordHasher::verify("pw", "", "salt") == false);
}
