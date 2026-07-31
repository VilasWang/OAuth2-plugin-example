#include <authforge/oauth2/jwk/JwkManager.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <fstream>
#include <sstream>
#include <cstring>

namespace authforge::oauth2
{

void JwkManager::log(authforge::common::ports::LogLevel level, const std::string &message) const
{
    if (logger_)
    {
        logger_->log(level, message);
    }
}

JwkManager::~JwkManager()
{
    if (rsaKey_)
    {
        EVP_PKEY_free(static_cast<EVP_PKEY *>(rsaKey_));
        rsaKey_ = nullptr;
    }
}

bool JwkManager::init(const Json::Value &config)
{
    if (initialized_)
    {
        log(
          authforge::common::ports::LogLevel::Error,
          "JwkManager: init() called more than once; ignoring "
          "(init-once-then-read-only contract). The existing key is kept."
        );
        return true;
    }

    const char *keyEnv = std::getenv("OAUTH2_SIGNING_KEY");
    if (keyEnv && std::strlen(keyEnv) > 0)
    {
        if (loadFromPem(keyEnv))
        {
            kid_ = config.get("kid", "key-1").asString();
            initialized_ = true;
            log(
              authforge::common::ports::LogLevel::Info,
              "JwkManager: Loaded signing key from OAUTH2_SIGNING_KEY env"
            );
            return true;
        }
    }

    const char *keyPathEnv = std::getenv("OAUTH2_JWT_KEY_PATH");
    if (keyPathEnv && std::strlen(keyPathEnv) > 0)
    {
        std::ifstream file(keyPathEnv);
        if (file.is_open())
        {
            std::stringstream ss;
            ss << file.rdbuf();
            if (loadFromPem(ss.str()))
            {
                kid_ = config.get("kid", "key-1").asString();
                initialized_ = true;
                log(
                  authforge::common::ports::LogLevel::Info,
                  "JwkManager: Loaded signing key from OAUTH2_JWT_KEY_PATH=" +
                    std::string(keyPathEnv)
                );
                return true;
            }
        }
        log(
          authforge::common::ports::LogLevel::Warn,
          "JwkManager: Failed to load key from OAUTH2_JWT_KEY_PATH=" + std::string(keyPathEnv)
        );
    }

    std::string keyPath = config.get("signing_key_path", "").asString();
    if (!keyPath.empty())
    {
        std::ifstream file(keyPath);
        if (file.is_open())
        {
            std::stringstream ss;
            ss << file.rdbuf();
            if (loadFromPem(ss.str()))
            {
                kid_ = config.get("kid", "key-1").asString();
                initialized_ = true;
                log(
                  authforge::common::ports::LogLevel::Info,
                  "JwkManager: Loaded signing key from " + keyPath
                );
                return true;
            }
        }
        log(
          authforge::common::ports::LogLevel::Warn, "JwkManager: Failed to load key from " + keyPath
        );
    }

    log(
      authforge::common::ports::LogLevel::Warn,
      "JwkManager: No signing key configured, generating ephemeral key (DEV ONLY)"
    );
    if (generateEphemeralKey())
    {
        kid_ = "ephemeral-dev-key";
        initialized_ = true;
        return true;
    }

    log(authforge::common::ports::LogLevel::Error, "JwkManager: Failed to initialize");
    return false;
}

bool JwkManager::loadFromPem(const std::string &pemData)
{
    BIO *bio = BIO_new_mem_buf(pemData.c_str(), static_cast<int>(pemData.length()));
    if (!bio)
        return false;

    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!pkey)
    {
        log(
          authforge::common::ports::LogLevel::Error, "JwkManager: Failed to parse PEM private key"
        );
        return false;
    }

    rsaKey_ = pkey;
    return true;
}

bool JwkManager::generateEphemeralKey()
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx)
        return false;

    if (EVP_PKEY_keygen_init(ctx) <= 0)
    {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0)
    {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    EVP_PKEY *pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
    {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    EVP_PKEY_CTX_free(ctx);
    rsaKey_ = pkey;
    return true;
}

namespace
{
// See JwkManager.h's top comment: byte-for-byte identical algorithm to
// authforge::drogon::adapters::OpenSslCryptoProvider::base64UrlEncode /
// authforge::common::testing::FakeCryptoProvider::base64UrlEncode
// (deliberately duplicated in each, for dependency-direction reasons).
constexpr char kBase64UrlAlphabet[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64UrlEncodeImpl(const unsigned char *bytes, size_t length)
{
    std::string out;
    out.reserve((length + 2) / 3 * 4);

    size_t i = 0;
    while (i + 3 <= length)
    {
        const uint32_t n = (static_cast<uint32_t>(bytes[i]) << 16) |
                           (static_cast<uint32_t>(bytes[i + 1]) << 8) |
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
}  // namespace

std::string JwkManager::base64UrlEncode(const unsigned char *data, size_t len)
{
    return base64UrlEncodeImpl(data, len);
}

std::string JwkManager::base64UrlEncode(const std::string &data)
{
    return base64UrlEncodeImpl(reinterpret_cast<const unsigned char *>(data.data()), data.size());
}

std::string JwkManager::signJwt(const Json::Value &payload) const
{
    if (!initialized_ || !rsaKey_)
    {
        log(
          authforge::common::ports::LogLevel::Error, "JwkManager: Cannot sign JWT - not initialized"
        );
        return "";
    }

    Json::Value header;
    header["alg"] = "RS256";
    header["typ"] = "JWT";
    header["kid"] = kid_;

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    std::string headerJson = Json::writeString(writerBuilder, header);
    std::string payloadJson = Json::writeString(writerBuilder, payload);

    std::string headerB64 = base64UrlEncode(headerJson);
    std::string payloadB64 = base64UrlEncode(payloadJson);

    std::string signingInput = headerB64 + "." + payloadB64;

    EVP_MD_CTX *mdCtx = EVP_MD_CTX_new();
    if (!mdCtx)
        return "";

    EVP_PKEY *pkey = static_cast<EVP_PKEY *>(rsaKey_);

    if (EVP_DigestSignInit(mdCtx, nullptr, EVP_sha256(), nullptr, pkey) <= 0)
    {
        EVP_MD_CTX_free(mdCtx);
        return "";
    }

    if (EVP_DigestSignUpdate(mdCtx, signingInput.c_str(), signingInput.length()) <= 0)
    {
        EVP_MD_CTX_free(mdCtx);
        return "";
    }

    size_t sigLen = 0;
    if (EVP_DigestSignFinal(mdCtx, nullptr, &sigLen) <= 0)
    {
        EVP_MD_CTX_free(mdCtx);
        return "";
    }

    std::vector<unsigned char> signature(sigLen);
    if (EVP_DigestSignFinal(mdCtx, signature.data(), &sigLen) <= 0)
    {
        EVP_MD_CTX_free(mdCtx);
        return "";
    }

    EVP_MD_CTX_free(mdCtx);

    std::string sigB64 = base64UrlEncode(signature.data(), sigLen);

    return signingInput + "." + sigB64;
}

bool JwkManager::getPublicKeyComponents(std::string &n, std::string &e) const
{
    if (!rsaKey_)
        return false;

    EVP_PKEY *pkey = static_cast<EVP_PKEY *>(rsaKey_);

    BIGNUM *bn_n = nullptr;
    BIGNUM *bn_e = nullptr;

    if (EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_N, &bn_n) <= 0 || !bn_n)
    {
        return false;
    }

    if (EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_E, &bn_e) <= 0 || !bn_e)
    {
        BN_free(bn_n);
        return false;
    }

    int nLen = BN_num_bytes(bn_n);
    std::vector<unsigned char> nBuf(nLen);
    BN_bn2bin(bn_n, nBuf.data());
    n = base64UrlEncode(nBuf.data(), nBuf.size());

    int eLen = BN_num_bytes(bn_e);
    std::vector<unsigned char> eBuf(eLen);
    BN_bn2bin(bn_e, eBuf.data());
    e = base64UrlEncode(eBuf.data(), eBuf.size());

    BN_free(bn_n);
    BN_free(bn_e);
    return true;
}

Json::Value JwkManager::getJwks() const
{
    Json::Value jwks;
    jwks["keys"] = Json::Value(Json::arrayValue);

    if (!initialized_)
        return jwks;

    std::string n, e;
    if (getPublicKeyComponents(n, e))
    {
        Json::Value key;
        key["kty"] = "RSA";
        key["use"] = "sig";
        key["alg"] = "RS256";
        key["kid"] = kid_;
        key["n"] = n;
        key["e"] = e;
        jwks["keys"].append(key);
    }

    return jwks;
}

}  // namespace authforge::oauth2
