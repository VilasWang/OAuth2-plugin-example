// Shared WebAuthn test-material builders (#142): hand-rolled CBOR encoding
// (RFC 8949), authData/attestationObject synthesis (W3C L2 §6.4/§6.5), and
// OpenSSL P-256/ECDSA helpers that produce REAL signatures. Extracted from
// WebAuthnCryptoTest.cc so the HTTP integration tests
// (tests/integration/controllers/WebAuthnHttpTest.cc) can drive the full
// begin->finish chains with genuine cryptographic material instead of
// mocks. Header-only under identity/testing/ per the established fixture
// convention (FakeOAuthHttpClient.h's header comment).

#pragma once

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <cstdint>
#include <memory>
#include <string>

namespace fulla::identity::testing::webauthn
{

// --- Hand-rolled CBOR encoding, used ONLY by the tests ---------------------
inline void appendHead(std::string &out, unsigned char major, uint64_t value)
{
    if (value < 24)
    {
        out.push_back(static_cast<char>((major << 5) | static_cast<unsigned char>(value)));
    }
    else if (value <= 0xFF)
    {
        out.push_back(static_cast<char>((major << 5) | 24));
        out.push_back(static_cast<char>(value));
    }
    else if (value <= 0xFFFF)
    {
        out.push_back(static_cast<char>((major << 5) | 25));
        out.push_back(static_cast<char>(value >> 8));
        out.push_back(static_cast<char>(value));
    }
    else if (value <= 0xFFFFFFFF)
    {
        out.push_back(static_cast<char>((major << 5) | 26));
        for (int shift = 24; shift >= 0; shift -= 8)
            out.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
    else
    {
        out.push_back(static_cast<char>((major << 5) | 27));
        for (int shift = 56; shift >= 0; shift -= 8)
            out.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}

inline void appendInt(std::string &out, int64_t value)
{
    if (value >= 0)
        appendHead(out, 0, static_cast<uint64_t>(value));
    else  // negative: -1 - magnitude
        appendHead(out, 1, static_cast<uint64_t>(-(value + 1)));
}

inline void appendBytes(std::string &out, const std::string &payload)
{
    appendHead(out, 2, payload.size());
    out += payload;
}

inline void appendText(std::string &out, const std::string &payload)
{
    appendHead(out, 3, payload.size());
    out += payload;
}

/// COSE ES256 key as map(5){1:kty, 3:alg, -1:crv, -2:x, -3:y} (RFC 9053).
inline std::string buildCoseKey(int64_t kty, int64_t alg, int64_t crv, const std::string &x, const std::string &y)
{
    std::string out;
    out.push_back('\xa5');  // map(5)
    appendInt(out, 1);
    appendInt(out, kty);
    appendInt(out, 3);
    appendInt(out, alg);
    appendInt(out, -1);
    appendInt(out, crv);
    appendInt(out, -2);
    appendBytes(out, x);
    appendInt(out, -3);
    appendBytes(out, y);
    return out;
}

// --- authData / attestationObject synthesis (L2 §6.4 / §6.5 layouts) -------
struct AuthDataSpec
{
    std::string rpIdHash;
    unsigned char flags = 0;  // bit0 UP, bit2 UV, bit6 AT, bit7 ED
    uint32_t signCount = 0;
    std::string aaguid;
    std::string credentialId;
    std::string coseKey;    // appended verbatim after the AT fields
    std::string extension;  // appended verbatim when the ED flag is set
};

inline std::string buildAuthData(const AuthDataSpec &spec)
{
    std::string out = spec.rpIdHash;
    out.push_back(static_cast<char>(spec.flags));
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<char>((spec.signCount >> shift) & 0xFF));

    if (spec.flags & 0x40)  // AT: attested credential data
    {
        out += spec.aaguid;
        const size_t credentialIdLength = spec.credentialId.size();
        out.push_back(static_cast<char>((credentialIdLength >> 8) & 0xFF));
        out.push_back(static_cast<char>(credentialIdLength & 0xFF));
        out += spec.credentialId;
        out += spec.coseKey;
    }
    if (spec.flags & 0x80)  // ED: extension data
        out += spec.extension;
    return out;
}

inline std::string buildAttestationObject(const std::string &authData, const std::string &fmt, bool emptyAttStmt)
{
    std::string out;
    out.push_back('\xa3');  // map(3)
    appendText(out, "fmt");
    appendText(out, fmt);
    appendText(out, "attStmt");
    if (emptyAttStmt)
    {
        out.push_back('\xa0');  // empty map -- the "none" format's shape
    }
    else
    {
        out.push_back('\xa1');
        appendText(out, "sig");
        appendBytes(out, std::string("\x01\x02", 2));
    }
    appendText(out, "authData");
    appendBytes(out, authData);
    return out;
}

// --- OpenSSL helpers --------------------------------------------------------
struct EvpPkeyDeleter
{
    void operator()(EVP_PKEY *key) const
    {
        EVP_PKEY_free(key);
    }
};
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

inline std::string sha256(const std::string &data)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    EVP_Digest(data.data(), data.size(), digest, nullptr, EVP_sha256(), nullptr);
    return std::string(reinterpret_cast<const char *>(digest), SHA256_DIGEST_LENGTH);
}

inline EvpPkeyPtr generateP256()
{
    struct CtxDeleter
    {
        void operator()(EVP_PKEY_CTX *ctx) const
        {
            EVP_PKEY_CTX_free(ctx);
        }
    };
    const std::unique_ptr<EVP_PKEY_CTX, CtxDeleter> ctx(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr));
    if (!ctx || EVP_PKEY_keygen_init(ctx.get()) != 1 || EVP_PKEY_CTX_set_group_name(ctx.get(), "prime256v1") != 1)
        return EvpPkeyPtr();

    EVP_PKEY *rawKey = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &rawKey) != 1)
        return EvpPkeyPtr();
    return EvpPkeyPtr(rawKey);
}

/// The public point's affine coordinates (x, y) as 32-byte strings.
inline bool exportXy(EVP_PKEY *key, std::string &x, std::string &y)
{
    unsigned char point[65] = {0};
    size_t pointLength = 0;
    if (EVP_PKEY_get_octet_string_param(
          key, OSSL_PKEY_PARAM_PUB_KEY, point, sizeof(point), &pointLength) != 1 ||
        pointLength != sizeof(point))
        return false;
    x.assign(reinterpret_cast<const char *>(point + 1), 32);
    y.assign(reinterpret_cast<const char *>(point + 33), 32);
    return true;
}

/// The WebAuthn signed message: authData || SHA-256(clientDataJSON).
inline std::string signedMessage(const std::string &authData, const std::string &clientDataJson)
{
    return authData + sha256(clientDataJson);
}

/// Real ECDSA/SHA-256 signature in ASN.1 DER (the alg -7 wire form).
inline std::string signEs256(EVP_PKEY *privateKey, const std::string &message)
{
    struct MdDeleter
    {
        void operator()(EVP_MD_CTX *ctx) const
        {
            EVP_MD_CTX_free(ctx);
        }
    };
    const std::unique_ptr<EVP_MD_CTX, MdDeleter> ctx(EVP_MD_CTX_new());
    if (!ctx || EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, privateKey) != 1)
        return std::string();

    size_t signatureLength = 0;
    if (EVP_DigestSign(ctx.get(), nullptr, &signatureLength,
                       reinterpret_cast<const unsigned char *>(message.data()), message.size()) != 1)
        return std::string();

    std::string signature(signatureLength, '\0');
    size_t written = signatureLength;
    if (EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char *>(&signature[0]), &written,
                       reinterpret_cast<const unsigned char *>(message.data()), message.size()) != 1)
        return std::string();
    signature.resize(written);
    return signature;
}

}  // namespace fulla::identity::testing::webauthn
