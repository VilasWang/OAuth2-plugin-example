#include <oauth2/utils/PasswordHasher.h>
#include <oauth2/adapters/OpenSslCryptoProvider.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace authforge::common::utils
{
// PBKDF2-SHA256 parameters (OWASP recommended minimum: 600,000 iterations for SHA-256)
static constexpr int PBKDF2_ITERATIONS = 310000;  // OWASP 2023 recommendation
static constexpr int PBKDF2_KEY_LENGTH = 32;      // 256 bits
static constexpr int PBKDF2_SALT_LENGTH = 16;     // 128 bits

// Output format: $pbkdf2-sha256$iterations$base64salt$base64hash
// This is compatible with Python's passlib and many other implementations.

static std::string bytesToHex(const unsigned char *data, size_t len)
{
    std::string hex;
    hex.reserve(len * 2);
    static const char hexChars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i)
    {
        hex.push_back(hexChars[data[i] >> 4]);
        hex.push_back(hexChars[data[i] & 0x0F]);
    }
    return hex;
}

static std::vector<unsigned char> hexToBytes(const std::string &hex)
{
    std::vector<unsigned char> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        unsigned char byte = 0;
        char hi = hex[i];
        char lo = hex[i + 1];
        byte = static_cast<unsigned char>(
          ((hi >= 'a' ? hi - 'a' + 10 : (hi >= 'A' ? hi - 'A' + 10 : hi - '0')) << 4) |
          (lo >= 'a' ? lo - 'a' + 10 : (lo >= 'A' ? lo - 'A' + 10 : lo - '0'))
        );
        bytes.push_back(byte);
    }
    return bytes;
}

std::string PasswordHasher::hash(const std::string &password)
{
    // Task 14 (design.md §5.6): migrated off drogon::utils::secureRandomBytes
    // onto the authforge::common::ports::ICryptoProvider Adapter
    // implementation (OpenSslCryptoProvider), same fallback shape.
    static authforge::drogon::adapters::OpenSslCryptoProvider cryptoProvider;

    // Generate random salt
    unsigned char salt[PBKDF2_SALT_LENGTH];
    if (!cryptoProvider.secureRandomBytes(salt, PBKDF2_SALT_LENGTH))
    {
        RAND_bytes(salt, PBKDF2_SALT_LENGTH);  // fallback
    }

    // Derive key using PBKDF2-SHA256
    unsigned char derivedKey[PBKDF2_KEY_LENGTH];
    int result = PKCS5_PBKDF2_HMAC(
      password.c_str(),
      static_cast<int>(password.length()),
      salt,
      PBKDF2_SALT_LENGTH,
      PBKDF2_ITERATIONS,
      EVP_sha256(),
      PBKDF2_KEY_LENGTH,
      derivedKey
    );

    if (result != 1)
    {
        throw std::runtime_error("PBKDF2 hashing failed");
    }

    // Format: $pbkdf2-sha256$310000$<hex-salt>$<hex-hash>
    std::string saltHex = bytesToHex(salt, PBKDF2_SALT_LENGTH);
    std::string hashHex = bytesToHex(derivedKey, PBKDF2_KEY_LENGTH);

    return "$pbkdf2-sha256$" + std::to_string(PBKDF2_ITERATIONS) + "$" + saltHex + "$" + hashHex;
}

bool PasswordHasher::verify(
  const std::string &password,
  const std::string &storedHash,
  const std::string &salt
)
{
    if (!needsRehash(storedHash))
    {
        // PBKDF2-SHA256 verification
        // Parse: $pbkdf2-sha256$iterations$hexsalt$hexhash
        if (storedHash.find("$pbkdf2-sha256$") != 0)
        {
            return false;
        }

        // Split by '$'
        std::vector<std::string> parts;
        std::string token;
        std::istringstream stream(storedHash);
        while (std::getline(stream, token, '$'))
        {
            if (!token.empty())
            {
                parts.push_back(token);
            }
        }

        // parts: ["pbkdf2-sha256", "310000", "<hexsalt>", "<hexhash>"]
        if (parts.size() != 4)
        {
            return false;
        }

        int iterations = std::stoi(parts[1]);
        auto saltBytes = hexToBytes(parts[2]);
        auto expectedHash = hexToBytes(parts[3]);

        // Derive key with same parameters
        unsigned char derivedKey[PBKDF2_KEY_LENGTH];
        int result = PKCS5_PBKDF2_HMAC(
          password.c_str(),
          static_cast<int>(password.length()),
          saltBytes.data(),
          static_cast<int>(saltBytes.size()),
          iterations,
          EVP_sha256(),
          PBKDF2_KEY_LENGTH,
          derivedKey
        );

        if (result != 1)
        {
            return false;
        }

        // Constant-time comparison
        if (expectedHash.size() != PBKDF2_KEY_LENGTH)
        {
            return false;
        }
        int diff = 0;
        for (int i = 0; i < PBKDF2_KEY_LENGTH; ++i)
        {
            diff |= derivedKey[i] ^ expectedHash[i];
        }
        return diff == 0;
    }
    else
    {
        // Legacy SHA-256 + salt verification. Task 14 (design.md §5.6):
        // migrated off drogon::utils::getSha256() onto
        // OpenSslCryptoProvider::sha256Hex(). sha256Hex() returns lowercase
        // hex (vs. drogon::utils::getSha256()'s uppercase), but this is
        // safe here (unlike CryptoUtils.h's hashToken()) because this
        // comparison already explicitly lowercases BOTH sides below before
        // comparing -- the legacy hash's stored case was never relied upon
        // to match exactly.
        static authforge::drogon::adapters::OpenSslCryptoProvider cryptoProvider;
        std::string inputHash = cryptoProvider.sha256Hex(password + salt);

        // Case-insensitive comparison (legacy hashes may vary in case)
        if (inputHash.length() != storedHash.length())
        {
            return false;
        }

        std::string inputLower = inputHash;
        std::string storedLower = storedHash;
        std::transform(inputLower.begin(), inputLower.end(), inputLower.begin(), ::tolower);
        std::transform(storedLower.begin(), storedLower.end(), storedLower.begin(), ::tolower);

        // Constant-time comparison
        int diff = 0;
        for (size_t i = 0; i < inputLower.length(); ++i)
        {
            diff |= inputLower[i] ^ storedLower[i];
        }
        return diff == 0;
    }
}

bool PasswordHasher::needsRehash(const std::string &storedHash)
{
    // PBKDF2-SHA256 hashes start with "$pbkdf2-sha256$"
    if (storedHash.find("$pbkdf2-sha256$") == 0)
    {
        return false;
    }
    // Everything else is legacy format (plain SHA-256 hex)
    return true;
}

}  // namespace authforge::common::utils
