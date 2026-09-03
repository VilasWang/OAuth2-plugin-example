#include <drogon/drogon_test.h>
#include <fulla/identity/TotpUtils.h>
#include <fulla/drogon/utils/CryptoUtils.h>

#include <chrono>

// #122: the drogon-static TotpUtils copy was deleted; these unit tests now
// exercise the identity-domain free functions through the real OpenSSL
// adapter. (Algorithm-level coverage with fakes + fixed timestamps lives in
// libs/identity/test/TotpUtilsTest.cc; this file keeps the real-provider
// smoke angle of the old drogon copy.)
namespace
{


int64_t testNowSeconds()
{
    return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
      ).count()
    );
}

}  // namespace

DROGON_TEST(Unit_P2_TotpUtils_GenerateSecret)
{
    auto secret = fulla::identity::totp::generateSecret(::fulla::drogon::utils::detail::cryptoProvider());
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
    auto secret = fulla::identity::totp::generateSecret(::fulla::drogon::utils::detail::cryptoProvider());
    auto code = fulla::identity::totp::generateCode(secret, testNowSeconds());
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
    auto secret = fulla::identity::totp::generateSecret(::fulla::drogon::utils::detail::cryptoProvider());
    auto code = fulla::identity::totp::generateCode(secret, testNowSeconds());
    CHECK(fulla::identity::totp::verifyCode(secret, code, testNowSeconds()) == true);
    CHECK(fulla::identity::totp::verifyCode(secret, "000000", testNowSeconds()) == false);
}

DROGON_TEST(Unit_P2_TotpUtils_GenerateBackupCodes)
{
    auto codes = fulla::identity::totp::generateBackupCodes(::fulla::drogon::utils::detail::cryptoProvider(), 10);
    CHECK(codes.size() == 10);
    for (const auto &code : codes)
    {
        CHECK(code.length() == 8);
    }
}

DROGON_TEST(Unit_P2_TotpUtils_OtpAuthUri)
{
    auto uri =
      fulla::identity::totp::generateOtpAuthUri("JBSWY3DPEHPK3PXP", "user@test.com", "TestApp");
    CHECK(uri.find("otpauth://totp/") == 0);
    CHECK(uri.find("secret=JBSWY3DPEHPK3PXP") != std::string::npos);
    CHECK(uri.find("issuer=TestApp") != std::string::npos);
}
