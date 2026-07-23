#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>
#include <oauth2/utils/PasswordHasher.h>

using namespace authforge::common::utils;

DROGON_TEST(Unit_PasswordHasher_HashFormat)
{
    auto hash = PasswordHasher::hash("testpassword");
    CHECK(hash.find("$pbkdf2-sha256$") == 0);
    CHECK(hash.length() > 50);
}

DROGON_TEST(Unit_PasswordHasher_VerifyCorrect)
{
    auto hash = PasswordHasher::hash("mypassword");
    CHECK(PasswordHasher::verify("mypassword", hash) == true);
}

DROGON_TEST(Unit_PasswordHasher_VerifyWrong)
{
    auto hash = PasswordHasher::hash("mypassword");
    CHECK(PasswordHasher::verify("wrongpassword", hash) == false);
}

DROGON_TEST(Unit_PasswordHasher_LegacyVerify)
{
    // Legacy SHA-256+salt format
    std::string salt = "test-salt";
    std::string password = "admin";
    std::string legacyHash = ::drogon::utils::getSha256(password + salt);
    CHECK(PasswordHasher::verify(password, legacyHash, salt) == true);
    CHECK(PasswordHasher::verify("wrong", legacyHash, salt) == false);
}

DROGON_TEST(Unit_PasswordHasher_NeedsRehash)
{
    auto pbkdf2Hash = PasswordHasher::hash("test");
    CHECK(PasswordHasher::needsRehash(pbkdf2Hash) == false);
    CHECK(PasswordHasher::needsRehash("abcdef1234567890") == true);
}
