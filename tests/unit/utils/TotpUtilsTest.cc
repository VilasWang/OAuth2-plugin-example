#include <drogon/drogon_test.h>
#include <authforge/drogon/utils/TotpUtils.h>

using namespace authforge::common::utils;

DROGON_TEST(Unit_P2_TotpUtils_GenerateSecret)
{
    auto secret = TotpUtils::generateSecret();
    CHECK(secret.length() == 32);  // 20 bytes base32 = 32 chars
    // All chars should be valid base32
    for (char c : secret)
    {
        bool validBase32 = (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7');
        CHECK(validBase32 == true);
    }
}

DROGON_TEST(Unit_P2_TotpUtils_GenerateCode)
{
    auto secret = TotpUtils::generateSecret();
    auto code = TotpUtils::generateCode(secret);
    CHECK(code.length() == 6);
    // All digits
    for (char c : code)
    {
        bool isDigit = (c >= '0' && c <= '9');
        CHECK(isDigit == true);
    }
}

DROGON_TEST(Unit_P2_TotpUtils_VerifyCode)
{
    auto secret = TotpUtils::generateSecret();
    auto code = TotpUtils::generateCode(secret);
    CHECK(TotpUtils::verifyCode(secret, code) == true);
    CHECK(TotpUtils::verifyCode(secret, "000000") == false);
}

DROGON_TEST(Unit_P2_TotpUtils_GenerateBackupCodes)
{
    auto codes = TotpUtils::generateBackupCodes(10);
    CHECK(codes.size() == 10);
    for (const auto &code : codes)
    {
        CHECK(code.length() == 8);
    }
}

DROGON_TEST(Unit_P2_TotpUtils_OtpAuthUri)
{
    auto uri = TotpUtils::generateOtpAuthUri("JBSWY3DPEHPK3PXP", "user@test.com", "TestApp");
    CHECK(uri.find("otpauth://totp/") == 0);
    CHECK(uri.find("secret=JBSWY3DPEHPK3PXP") != std::string::npos);
    CHECK(uri.find("issuer=TestApp") != std::string::npos);
}
