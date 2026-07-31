#include <authforge/identity/TotpUtils.h>

#include <openssl/hmac.h>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace authforge::identity
{

namespace totp
{

namespace
{

constexpr char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

std::string base32Encode(const uint8_t *data, size_t len)
{
    std::string result;
    result.reserve((len * 8 + 4) / 5);

    int buffer = 0;
    int bitsLeft = 0;

    for (size_t i = 0; i < len; ++i)
    {
        buffer = (buffer << 8) | data[i];
        bitsLeft += 8;
        while (bitsLeft >= 5)
        {
            result += kBase32Alphabet[(buffer >> (bitsLeft - 5)) & 0x1F];
            bitsLeft -= 5;
        }
    }

    if (bitsLeft > 0)
    {
        result += kBase32Alphabet[(buffer << (5 - bitsLeft)) & 0x1F];
    }

    return result;
}

std::vector<uint8_t> base32Decode(const std::string &encoded)
{
    std::vector<uint8_t> result;
    int buffer = 0;
    int bitsLeft = 0;

    for (char c : encoded)
    {
        int val = -1;
        if (c >= 'A' && c <= 'Z')
            val = c - 'A';
        else if (c >= 'a' && c <= 'z')
            val = c - 'a';
        else if (c >= '2' && c <= '7')
            val = c - '2' + 26;
        else
            continue;  // Skip padding/invalid

        buffer = (buffer << 5) | val;
        bitsLeft += 5;

        if (bitsLeft >= 8)
        {
            result.push_back(static_cast<uint8_t>((buffer >> (bitsLeft - 8)) & 0xFF));
            bitsLeft -= 8;
        }
    }

    return result;
}

uint32_t generateOtp(const std::vector<uint8_t> &key, uint64_t counter)
{
    uint8_t counterBytes[8];
    for (int i = 7; i >= 0; --i)
    {
        counterBytes[i] = static_cast<uint8_t>(counter & 0xFF);
        counter >>= 8;
    }

    unsigned char hmacResult[20];
    unsigned int hmacLen = 0;
    HMAC(
      EVP_sha1(), key.data(), static_cast<int>(key.size()), counterBytes, 8, hmacResult, &hmacLen
    );

    int offset = hmacResult[19] & 0x0F;
    uint32_t code = ((hmacResult[offset] & 0x7F) << 24) | ((hmacResult[offset + 1] & 0xFF) << 16) |
                    ((hmacResult[offset + 2] & 0xFF) << 8) | (hmacResult[offset + 3] & 0xFF);

    return code % 1000000;  // 6 digits
}

std::string formatSixDigits(uint32_t otp)
{
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(6) << otp;
    return ss.str();
}

}  // namespace

std::string generateSecret(authforge::common::ports::ICryptoProvider &crypto)
{
    uint8_t secretBytes[20];  // 160 bits
    crypto.secureRandomBytes(secretBytes, 20);
    return base32Encode(secretBytes, 20);
}

std::string generateCode(const std::string &secret, int64_t nowSeconds)
{
    auto key = base32Decode(secret);
    if (key.empty())
        return "";

    uint64_t timeStep = static_cast<uint64_t>(nowSeconds) / 30;
    return formatSixDigits(generateOtp(key, timeStep));
}

bool verifyCode(const std::string &secret, const std::string &code, int64_t nowSeconds)
{
    if (code.length() != 6)
        return false;

    auto key = base32Decode(secret);
    if (key.empty())
        return false;

    uint64_t timeStep = static_cast<uint64_t>(nowSeconds) / 30;

    // Allow +/- 1 time step for clock skew.
    for (int i = -1; i <= 1; ++i)
    {
        if (formatSixDigits(generateOtp(key, timeStep + i)) == code)
            return true;
    }

    return false;
}

std::string generateOtpAuthUri(
  const std::string &secret,
  const std::string &accountName,
  const std::string &issuer
)
{
    return "otpauth://totp/" + issuer + ":" + accountName + "?secret=" + secret +
           "&issuer=" + issuer + "&algorithm=SHA1&digits=6&period=30";
}

std::vector<std::string> generateBackupCodes(
  authforge::common::ports::ICryptoProvider &crypto,
  int count
)
{
    std::vector<std::string> codes;
    codes.reserve(count);

    static const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";  // No I,O,0,1 (ambiguous)

    for (int i = 0; i < count; ++i)
    {
        uint8_t randomBytes[8];
        crypto.secureRandomBytes(randomBytes, 8);

        std::string code;
        for (int j = 0; j < 8; ++j)
        {
            code += charset[randomBytes[j] % (sizeof(charset) - 1)];
        }
        codes.push_back(code);
    }

    return codes;
}

}  // namespace totp

}  // namespace authforge::identity
