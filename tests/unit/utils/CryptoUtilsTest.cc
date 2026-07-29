#include <drogon/drogon_test.h>
#include <authforge/drogon/utils/CryptoUtils.h>

using namespace authforge::drogon::utils;

DROGON_TEST(Unit_P2_CryptoUtils_GenerateSecureToken)
{
    auto token = generateSecureToken();
    CHECK(token.length() == 43);  // 32 bytes base64url = 43 chars
    // Should not contain + or / (base64url uses - and _)
    CHECK(token.find('+') == std::string::npos);
    CHECK(token.find('/') == std::string::npos);
}

DROGON_TEST(Unit_P2_CryptoUtils_GenerateSecureTokenUnique)
{
    auto t1 = generateSecureToken();
    auto t2 = generateSecureToken();
    CHECK(t1 != t2);
}

DROGON_TEST(Unit_P2_CryptoUtils_HashToken)
{
    auto hash = hashToken("test-token");
    CHECK(hash.length() == 64);  // SHA-256 hex = 64 chars
    // All hex characters (case-insensitive)
    for (char c : hash)
    {
        bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        CHECK(isHex == true);
    }
}

DROGON_TEST(Unit_P2_CryptoUtils_HashTokenDeterministic)
{
    auto h1 = hashToken("same-input");
    auto h2 = hashToken("same-input");
    CHECK(h1 == h2);
}

DROGON_TEST(Unit_P2_CryptoUtils_HashTokenDifferentInputs)
{
    auto h1 = hashToken("input-a");
    auto h2 = hashToken("input-b");
    CHECK(h1 != h2);
}
