#include <oauth2/utils/JwkManager.h>
#include <drogon/drogon.h>
#include <oauth2/adapters/OpenSslCryptoProvider.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <fstream>
#include <sstream>
#include <cstring>

namespace oauth2
{

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
    // init-once guard (defect 1.5): init() is the only mutating method on this
    // class. Once a previous call has succeeded, refuse to run again — log an
    // error and no-op (do NOT re-allocate rsaKey_ or overwrite kid_). This
    // removes the run-time mutation entry point so concurrent readers
    // (signJwt/getJwks/getKeyId) can never observe the key state changing.
    if (initialized_)
    {
        LOG_ERROR << "JwkManager: init() called more than once; ignoring "
                     "(init-once-then-read-only contract). The existing key is kept.";
        return true;
    }

    // Try loading from environment variable (PEM content)
    const char *keyEnv = std::getenv("OAUTH2_SIGNING_KEY");
    if (keyEnv && std::strlen(keyEnv) > 0)
    {
        if (loadFromPem(keyEnv))
        {
            kid_ = config.get("kid", "key-1").asString();
            initialized_ = true;
            LOG_INFO << "JwkManager: Loaded signing key from OAUTH2_SIGNING_KEY env";
            return true;
        }
    }

    // Try loading from file path (env variable)
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
                LOG_INFO << "JwkManager: Loaded signing key from OAUTH2_JWT_KEY_PATH="
                         << keyPathEnv;
                return true;
            }
        }
        LOG_WARN << "JwkManager: Failed to load key from OAUTH2_JWT_KEY_PATH=" << keyPathEnv;
    }

    // Try loading from config file path
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
                LOG_INFO << "JwkManager: Loaded signing key from " << keyPath;
                return true;
            }
        }
        LOG_WARN << "JwkManager: Failed to load key from " << keyPath;
    }

    // Fallback: generate ephemeral key (dev/test only)
    LOG_WARN << "JwkManager: No signing key configured, generating ephemeral key (DEV ONLY)";
    if (generateEphemeralKey())
    {
        kid_ = "ephemeral-dev-key";
        initialized_ = true;
        return true;
    }

    LOG_ERROR << "JwkManager: Failed to initialize";
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
        LOG_ERROR << "JwkManager: Failed to parse PEM private key";
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

std::string JwkManager::base64UrlEncode(const unsigned char *data, size_t len)
{
    // Task 14 (design.md §5.6): migrated off
    // drogon::utils::base64EncodeUnpadded onto the
    // authforge::common::ports::ICryptoProvider Adapter implementation
    // (OpenSslCryptoProvider).
    static oauth2::adapters::OpenSslCryptoProvider cryptoProvider;
    return cryptoProvider.base64UrlEncode(data, len);
}

std::string JwkManager::base64UrlEncode(const std::string &data)
{
    static oauth2::adapters::OpenSslCryptoProvider cryptoProvider;
    return cryptoProvider.base64UrlEncode(data);
}

std::string JwkManager::signJwt(const Json::Value &payload) const
{
    // OpenSSL concurrency (defect 1.5): this const method is safe to call from
    // many request threads at once because the per-call signing context below
    // (EVP_MD_CTX) is created with EVP_MD_CTX_new() and released with
    // EVP_MD_CTX_free() ON EVERY CALL — each thread gets its own, so there is
    // NO shared mutable signing context. The ONLY object shared across
    // concurrent signers is the EVP_PKEY (rsaKey_): concurrent signing touches
    // its internal state (refcount, BN_BLINDING, lazily-initialized
    // BN_MONT_CTX). This design ASSUMES OpenSSL >= 1.1.0 built with thread
    // support (atomic refcounts + automatic internal locking), which makes that
    // shared EVP_PKEY use thread-safe. If this project may instead link against
    // OpenSSL 1.0.2 or earlier, concurrent signing would be a data race unless
    // the application registers the legacy CRYPTO_set_locking_callback /
    // CRYPTO_THREADID callbacks — prefer upgrading OpenSSL to >= 1.1.0.
    if (!initialized_ || !rsaKey_)
    {
        LOG_ERROR << "JwkManager: Cannot sign JWT - not initialized";
        return "";
    }

    // Build header
    Json::Value header;
    header["alg"] = "RS256";
    header["typ"] = "JWT";
    header["kid"] = kid_;

    // Serialize header and payload
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    std::string headerJson = Json::writeString(writerBuilder, header);
    std::string payloadJson = Json::writeString(writerBuilder, payload);

    // Base64URL encode
    std::string headerB64 = base64UrlEncode(headerJson);
    std::string payloadB64 = base64UrlEncode(payloadJson);

    // Signing input
    std::string signingInput = headerB64 + "." + payloadB64;

    // Sign with RS256 (SHA-256 + RSA PKCS#1 v1.5)
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

    // Get signature length
    size_t sigLen = 0;
    if (EVP_DigestSignFinal(mdCtx, nullptr, &sigLen) <= 0)
    {
        EVP_MD_CTX_free(mdCtx);
        return "";
    }

    // Get signature
    std::vector<unsigned char> signature(sigLen);
    if (EVP_DigestSignFinal(mdCtx, signature.data(), &sigLen) <= 0)
    {
        EVP_MD_CTX_free(mdCtx);
        return "";
    }

    EVP_MD_CTX_free(mdCtx);

    // Base64URL encode signature
    std::string sigB64 = base64UrlEncode(signature.data(), sigLen);

    return signingInput + "." + sigB64;
}

bool JwkManager::getPublicKeyComponents(std::string &n, std::string &e) const
{
    if (!rsaKey_)
        return false;

    EVP_PKEY *pkey = static_cast<EVP_PKEY *>(rsaKey_);

    // OpenSSL 3.x migration (Task 3 / design §9.3): EVP_PKEY_get1_RSA +
    // RSA_get0_key + BN_* accessors are deprecated as of OpenSSL 3.0. Read
    // the modulus/exponent directly as BIGNUMs via EVP_PKEY_get_bn_param(),
    // which works on the EVP_PKEY without extracting a legacy RSA* handle.
    // BN_bn2bin() semantics (big-endian, minimal-length encoding) are
    // unchanged, so the resulting base64url bytes are byte-for-byte
    // identical to the pre-migration output (preservation 3.3 / golden).
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

    // Convert BIGNUM to binary then base64url
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

}  // namespace oauth2
