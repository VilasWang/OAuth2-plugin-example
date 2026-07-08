#include <authforge/common/testing/FakeCryptoProvider.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

namespace authforge::common::testing
{

namespace
{

// Same base64url alphabet/encoding logic as
// OAuth2Plugin/src/adapters/OpenSslCryptoProvider.cc -- duplicated
// deliberately (see this file's own header comment on why this library
// must not depend on OAuth2Plugin). Real cryptographic/encoding primitives
// stay REAL here (only secureRandomBytes is faked), so this is not a
// "fake" encoder, just the same real algorithm re-implemented in a
// dependency-direction-correct location.
constexpr char kBase64UrlAlphabet[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64UrlEncodeImpl(const unsigned char *bytes, size_t length)
{
    std::string out;
    out.reserve((length + 2) / 3 * 4);

    size_t i = 0;
    while (i + 3 <= length)
    {
        const uint32_t n =
          (static_cast<uint32_t>(bytes[i]) << 16) | (static_cast<uint32_t>(bytes[i + 1]) << 8) |
          static_cast<uint32_t>(bytes[i + 2]);
        out.push_back(kBase64UrlAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(n >> 12) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(n >> 6) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[n & 0x3F]);
        i += 3;
    }

    const size_t remaining = length - i;
    if (remaining == 1)
    {
        const uint32_t n = static_cast<uint32_t>(bytes[i]) << 16;
        out.push_back(kBase64UrlAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(n >> 12) & 0x3F]);
    }
    else if (remaining == 2)
    {
        const uint32_t n =
          (static_cast<uint32_t>(bytes[i]) << 16) | (static_cast<uint32_t>(bytes[i + 1]) << 8);
        out.push_back(kBase64UrlAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(n >> 12) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(n >> 6) & 0x3F]);
    }

    return out;
}

int base64UrlCharValue(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '-')
        return 62;
    if (c == '_')
        return 63;
    return -1;
}

std::vector<unsigned char> base64UrlDecodeImpl(const std::string &encoded)
{
    std::vector<unsigned char> out;
    out.reserve(encoded.size() / 4 * 3 + 3);

    int buffer = 0;
    int bitsCollected = 0;

    for (unsigned char c : encoded)
    {
        const int value = base64UrlCharValue(c);
        if (value < 0)
        {
            return {};
        }
        buffer = (buffer << 6) | value;
        bitsCollected += 6;
        if (bitsCollected >= 8)
        {
            bitsCollected -= 8;
            out.push_back(static_cast<unsigned char>((buffer >> bitsCollected) & 0xFF));
        }
    }

    return out;
}

}  // namespace

uint64_t FakeCryptoProvider::nextRandom64()
{
    // xorshift64* (Vigna, 2014): a fast, simple, well-known deterministic
    // PRNG -- NOT cryptographically secure (by design, this is a fake for
    // test determinism, never for production use). A zero seed is nudged
    // to a fixed non-zero constant, since xorshift is degenerate at 0.
    if (state_ == 0)
    {
        state_ = 0x9E3779B97F4A7C15ULL;
    }
    uint64_t x = state_;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    state_ = x;
    return x * 0x2545F4914F6CDD1DULL;
}

std::vector<unsigned char> FakeCryptoProvider::sha256(const std::string &data)
{
    std::vector<unsigned char> digest(EVP_MAX_MD_SIZE);
    unsigned int digestLen = 0;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
        return {};

    if (
      EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(ctx, data.data(), data.size()) != 1 ||
      EVP_DigestFinal_ex(ctx, digest.data(), &digestLen) != 1)
    {
        EVP_MD_CTX_free(ctx);
        return {};
    }

    EVP_MD_CTX_free(ctx);
    digest.resize(digestLen);
    return digest;
}

std::string FakeCryptoProvider::sha256Hex(const std::string &data)
{
    const auto digest = sha256(data);
    static const char hexChars[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (unsigned char b : digest)
    {
        hex.push_back(hexChars[b >> 4]);
        hex.push_back(hexChars[b & 0x0F]);
    }
    return hex;
}

bool FakeCryptoProvider::secureRandomBytes(unsigned char *buffer, size_t length)
{
    size_t i = 0;
    while (i < length)
    {
        uint64_t word = nextRandom64();
        for (int b = 0; b < 8 && i < length; ++b, ++i)
        {
            buffer[i] = static_cast<unsigned char>((word >> (8 * b)) & 0xFF);
        }
    }
    return true;
}

std::string FakeCryptoProvider::base64UrlEncode(const unsigned char *bytes, size_t length)
{
    return base64UrlEncodeImpl(bytes, length);
}

std::string FakeCryptoProvider::base64UrlEncode(const std::string &data)
{
    return base64UrlEncodeImpl(reinterpret_cast<const unsigned char *>(data.data()), data.size());
}

std::vector<unsigned char> FakeCryptoProvider::base64UrlDecode(const std::string &encoded)
{
    return base64UrlDecodeImpl(encoded);
}

std::vector<unsigned char> FakeCryptoProvider::hmacSha256(
  const std::string &key,
  const std::string &data
)
{
    std::vector<unsigned char> result(EVP_MAX_MD_SIZE);
    unsigned int resultLen = 0;

    unsigned char *ret = HMAC(
      EVP_sha256(),
      key.data(),
      static_cast<int>(key.size()),
      reinterpret_cast<const unsigned char *>(data.data()),
      data.size(),
      result.data(),
      &resultLen
    );

    if (!ret)
        return {};

    result.resize(resultLen);
    return result;
}

std::vector<unsigned char> FakeCryptoProvider::pbkdf2HmacSha256(
  const std::string &password,
  const std::string &salt,
  int iterations,
  size_t keyLength
)
{
    std::vector<unsigned char> derivedKey(keyLength);
    const int result = PKCS5_PBKDF2_HMAC(
      password.c_str(),
      static_cast<int>(password.length()),
      reinterpret_cast<const unsigned char *>(salt.data()),
      static_cast<int>(salt.size()),
      iterations,
      EVP_sha256(),
      static_cast<int>(keyLength),
      derivedKey.data()
    );

    if (result != 1)
        return {};

    return derivedKey;
}

std::vector<unsigned char> FakeCryptoProvider::rsaSign(
  const std::string &privateKeyPem,
  const std::string &digestAlgorithm,
  const std::string &data
)
{
    BIO *bio = BIO_new_mem_buf(privateKeyPem.data(), static_cast<int>(privateKeyPem.size()));
    if (!bio)
        return {};

    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey)
        return {};

    const EVP_MD *md = EVP_get_digestbyname(digestAlgorithm.c_str());
    if (!md)
    {
        EVP_PKEY_free(pkey);
        return {};
    }

    EVP_MD_CTX *mdCtx = EVP_MD_CTX_new();
    if (!mdCtx)
    {
        EVP_PKEY_free(pkey);
        return {};
    }

    std::vector<unsigned char> signature;
    if (EVP_DigestSignInit(mdCtx, nullptr, md, nullptr, pkey) <= 0)
    {
        EVP_MD_CTX_free(mdCtx);
        EVP_PKEY_free(pkey);
        return {};
    }

    if (EVP_DigestSignUpdate(mdCtx, data.data(), data.size()) <= 0)
    {
        EVP_MD_CTX_free(mdCtx);
        EVP_PKEY_free(pkey);
        return {};
    }

    size_t sigLen = 0;
    if (EVP_DigestSignFinal(mdCtx, nullptr, &sigLen) <= 0)
    {
        EVP_MD_CTX_free(mdCtx);
        EVP_PKEY_free(pkey);
        return {};
    }

    signature.resize(sigLen);
    if (EVP_DigestSignFinal(mdCtx, signature.data(), &sigLen) <= 0)
    {
        EVP_MD_CTX_free(mdCtx);
        EVP_PKEY_free(pkey);
        return {};
    }
    signature.resize(sigLen);

    EVP_MD_CTX_free(mdCtx);
    EVP_PKEY_free(pkey);
    return signature;
}

}  // namespace authforge::common::testing
