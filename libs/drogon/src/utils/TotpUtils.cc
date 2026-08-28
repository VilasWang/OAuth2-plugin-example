#include <fulla/drogon/utils/TotpUtils.h>
#include <fulla/drogon/adapters/OpenSslCryptoProvider.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <chrono>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace fulla::common::utils
{
static const char BASE32_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

std::string TotpUtils::base32Encode(const uint8_t *data, size_t len)
{
    std::string result;
    result.reserve((len * 8 + 4) / 5);

    // #104 (second ASan+UBSan pass): same accumulation bug as the identity
    // lib's TotpUtils — `int buffer` grew 5-8 bits per input unit and
    // overflowed into signed left-shift territory (UB that happened to wrap
    // benignly on two's-complement targets). uint32_t buffer + truncate to
    // the live bits after each emit; emitted values read exactly those bits,
    // so outputs are byte-identical to the historical wrapped-int behavior.
    uint32_t buffer = 0;
    int bitsLeft = 0;

    for (size_t i = 0; i < len; ++i)
    {
        buffer = (buffer << 8) | data[i];
        bitsLeft += 8;
        while (bitsLeft >= 5)
        {
            result += BASE32_ALPHABET[(buffer >> (bitsLeft - 5)) & 0x1F];
            bitsLeft -= 5;
        }
        buffer &= (bitsLeft == 0) ? 0u : ((1u << bitsLeft) - 1);
    }

    if (bitsLeft > 0)
    {
        result += BASE32_ALPHABET[(buffer << (5 - bitsLeft)) & 0x1F];
    }

    return result;
}

std::vector<uint8_t> TotpUtils::base32Decode(const std::string &encoded)
{
    std::vector<uint8_t> result;
    uint32_t buffer = 0;
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

        buffer = (buffer << 5) | static_cast<uint32_t>(val);
        bitsLeft += 5;

        if (bitsLeft >= 8)
        {
            result.push_back(static_cast<uint8_t>((buffer >> (bitsLeft - 8)) & 0xFF));
            bitsLeft -= 8;
            // Same truncate-to-live-bits discipline as base32Encode.
            buffer &= (bitsLeft == 0) ? 0u : ((1u << bitsLeft) - 1);
        }
    }

    return result;
}

std::string TotpUtils::generateSecret()
{
    // Task 14 (design.md §5.6): migrated off drogon::utils::secureRandomBytes
    // onto the fulla::common::ports::ICryptoProvider Adapter
    // implementation (OpenSslCryptoProvider), same fallback shape.
    static fulla::drogon::adapters::OpenSslCryptoProvider cryptoProvider;

    uint8_t secretBytes[20];  // 160 bits
    if (!cryptoProvider.secureRandomBytes(secretBytes, 20))
    {
        RAND_bytes(secretBytes, 20);
    }
    return base32Encode(secretBytes, 20);
}

uint32_t TotpUtils::generateOtp(const std::vector<uint8_t> &key, uint64_t counter)
{
    // Convert counter to big-endian 8 bytes
    uint8_t counterBytes[8];
    for (int i = 7; i >= 0; --i)
    {
        counterBytes[i] = static_cast<uint8_t>(counter & 0xFF);
        counter >>= 8;
    }

    // HMAC-SHA1
    unsigned char hmacResult[20];
    unsigned int hmacLen = 0;
    HMAC(
      EVP_sha1(), key.data(), static_cast<int>(key.size()), counterBytes, 8, hmacResult, &hmacLen
    );

    // Dynamic truncation (RFC 4226 Section 5.4)
    int offset = hmacResult[19] & 0x0F;
    uint32_t code = ((hmacResult[offset] & 0x7F) << 24) | ((hmacResult[offset + 1] & 0xFF) << 16) |
                    ((hmacResult[offset + 2] & 0xFF) << 8) | (hmacResult[offset + 3] & 0xFF);

    return code % 1000000;  // 6 digits
}

std::string TotpUtils::generateCode(const std::string &secret)
{
    auto key = base32Decode(secret);
    if (key.empty())
        return "";

    uint64_t timeStep = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                std::chrono::system_clock::now().time_since_epoch()
                        )
                                                .count()) /
                        30;

    uint32_t otp = generateOtp(key, timeStep);

    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(6) << otp;
    return ss.str();
}

bool TotpUtils::verifyCode(const std::string &secret, const std::string &code)
{
    if (code.length() != 6)
        return false;

    auto key = base32Decode(secret);
    if (key.empty())
        return false;

    uint64_t timeStep = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                std::chrono::system_clock::now().time_since_epoch()
                        )
                                                .count()) /
                        30;

    // Allow +/- 1 time step for clock skew
    for (int i = -1; i <= 1; ++i)
    {
        uint32_t otp = generateOtp(key, timeStep + i);
        std::ostringstream ss;
        ss << std::setfill('0') << std::setw(6) << otp;
        if (ss.str() == code)
            return true;
    }

    return false;
}

std::string TotpUtils::generateOtpAuthUri(
  const std::string &secret,
  const std::string &accountName,
  const std::string &issuer
)
{
    return "otpauth://totp/" + issuer + ":" + accountName + "?secret=" + secret +
           "&issuer=" + issuer + "&algorithm=SHA1&digits=6&period=30";
}

std::vector<std::string> TotpUtils::generateBackupCodes(int count)
{
    std::vector<std::string> codes;
    codes.reserve(count);

    static const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";  // No I,O,0,1 (ambiguous)

    for (int i = 0; i < count; ++i)
    {
        uint8_t randomBytes[8];
        // Task 14 (design.md §5.6): migrated off
        // drogon::utils::secureRandomBytes onto OpenSslCryptoProvider.
        static fulla::drogon::adapters::OpenSslCryptoProvider cryptoProvider;
        if (!cryptoProvider.secureRandomBytes(randomBytes, 8))
        {
            RAND_bytes(randomBytes, 8);
        }

        std::string code;
        for (int j = 0; j < 8; ++j)
        {
            code += charset[randomBytes[j] % (sizeof(charset) - 1)];
        }
        codes.push_back(code);
    }

    return codes;
}

}  // namespace fulla::common::utils
