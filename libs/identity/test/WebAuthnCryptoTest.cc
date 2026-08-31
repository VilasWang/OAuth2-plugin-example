// #142 (WebAuthn real-signature verification): unit tests for the pure
// WebAuthn L2 parsing/verification primitives in WebAuthnCrypto.{h,cc}.
//
// Everything a real browser/authenticator would send is synthesized here
// from first principles so the tests never trust the code under test to
// build its own fixtures:
//   - a P-256 keypair is generated through OpenSSL's EVP interface (the
//     same provider stack the verification code uses),
//   - the COSE ES256 key, authenticatorData and attestationObject bytes
//     are hand-encoded CBOR via the little append* helpers below,
//   - signatures are produced with EVP_DigestSign (real ECDSA over
//     SHA-256, ASN.1 DER -- exactly the alg -7 wire format).

#include "../src/webauthn/WebAuthnCrypto.h"

#include <gtest/gtest.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

using fulla::identity::webauthn::parseAttestationObject;
using fulla::identity::webauthn::parseAuthData;
using fulla::identity::webauthn::parseClientDataJSON;
using fulla::identity::webauthn::parseCoseKeyEs256;
using fulla::identity::webauthn::verifyEs256Signature;

namespace
{

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------
const std::string kRpId = "example.com";
const std::string kOrigin = "https://example.com";
const std::string kChallenge = "c2hhbGxlbmdl";  // base64url, compared verbatim
const std::string kClientDataJson =
    "{\"type\":\"webauthn.create\",\"challenge\":\"" + kChallenge + "\",\"origin\":\"" + kOrigin + "\"}";
constexpr uint32_t kSignCount = 0x01020304u;
const std::string kCredentialId = "credential-id-16";  // exactly 16 bytes
const std::string kAaguid = std::string(16, '\x11');   // 16 raw bytes

// ---------------------------------------------------------------------------
// Hand-rolled CBOR encoding (RFC 8949), used ONLY by the tests
// ---------------------------------------------------------------------------
void appendHead(std::string &out, unsigned char major, uint64_t value)
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

void appendUint(std::string &out, uint64_t value)
{
    appendHead(out, 0, value);
}

void appendInt(std::string &out, int64_t value)
{
    if (value >= 0)
        appendHead(out, 0, static_cast<uint64_t>(value));
    else  // negative: -1 - magnitude, magnitude >= 0 always fits uint64 here
        appendHead(out, 1, static_cast<uint64_t>(-(value + 1)));
}

void appendBytes(std::string &out, const std::string &payload)
{
    appendHead(out, 2, payload.size());
    out += payload;
}

void appendText(std::string &out, const std::string &payload)
{
    appendHead(out, 3, payload.size());
    out += payload;
}

// COSE ES256 key as map(5){1:kty, 3:alg, -1:crv, -2:x, -3:y} (RFC 9053).
// The individual values are parameters so negative tests can corrupt one
// field at a time.
std::string buildCoseKey(int64_t kty, int64_t alg, int64_t crv, const std::string &x, const std::string &y)
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

// ---------------------------------------------------------------------------
// authData / attestationObject synthesis (L2 §6.4 / §6.5 layouts)
// ---------------------------------------------------------------------------
struct AuthDataSpec
{
    std::string rpIdHash;
    unsigned char flags = 0;
    uint32_t signCount = 0;
    std::string aaguid;
    std::string credentialId;
    std::string coseKey;    // appended verbatim after the AT fields
    std::string extension;  // appended verbatim when the ED flag is set
};

std::string buildAuthData(const AuthDataSpec &spec)
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

std::string buildAttestationObject(const std::string &authData, const std::string &fmt, bool emptyAttStmt)
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
        // A non-empty stand-in: {"sig": h'0102'}.
        out.push_back('\xa1');
        appendText(out, "sig");
        appendBytes(out, std::string("\x01\x02", 2));
    }
    appendText(out, "authData");
    appendBytes(out, authData);
    return out;
}

// ---------------------------------------------------------------------------
// OpenSSL helpers
// ---------------------------------------------------------------------------
struct EvpPkeyDeleter
{
    void operator()(EVP_PKEY *key) const
    {
        EVP_PKEY_free(key);
    }
};
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

std::string sha256(const std::string &data)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    EVP_Digest(data.data(), data.size(), digest, nullptr, EVP_sha256(), nullptr);
    return std::string(reinterpret_cast<const char *>(digest), SHA256_DIGEST_LENGTH);
}

EvpPkeyPtr generateP256()
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

// The WebAuthn signed message: authData || SHA-256(clientDataJSON).
std::string signedMessage(const std::string &authData, const std::string &clientDataJson)
{
    return authData + sha256(clientDataJson);
}

// Real ECDSA/SHA-256 signature in ASN.1 DER (the alg -7 wire form).
std::string signEs256(EVP_PKEY *privateKey, const std::string &message)
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

}  // namespace

// ---------------------------------------------------------------------------
// Fixture: one P-256 keypair per test, x/y/COSE derived from it.
// ---------------------------------------------------------------------------
class WebAuthnCryptoTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        key_ = generateP256();
        ASSERT_TRUE(key_ != nullptr);

        // The provider exports the public key as the SEC1 uncompressed
        // point 0x04 || x || y -- split it into the affine coordinates.
        unsigned char point[65] = {0};
        size_t pointLength = 0;
        ASSERT_EQ(EVP_PKEY_get_octet_string_param(key_.get(), OSSL_PKEY_PARAM_PUB_KEY, point, sizeof(point),
                                                 &pointLength),
                  1);
        ASSERT_EQ(pointLength, sizeof(point));
        x_.assign(reinterpret_cast<const char *>(point + 1), 32);
        y_.assign(reinterpret_cast<const char *>(point + 33), 32);

        coseKey_ = buildCoseKey(2 /*kty EC2*/, -7 /*alg ES256*/, 1 /*crv P-256*/, x_, y_);
    }

    // A well-formed registration authData: UP|UV|AT, fixed signCount,
    // aaguid/credentialId/COSE key.
    std::string registrationAuthData(unsigned char flagOverrides = 0x45 /*UP|UV|AT*/) const
    {
        AuthDataSpec spec;
        spec.rpIdHash = sha256(kRpId);
        spec.flags = flagOverrides;
        spec.signCount = kSignCount;
        spec.aaguid = kAaguid;
        spec.credentialId = kCredentialId;
        spec.coseKey = coseKey_;
        return buildAuthData(spec);
    }

    EvpPkeyPtr key_;
    std::string x_;
    std::string y_;
    std::string coseKey_;
};

// ---------------------------------------------------------------------------
// parseClientDataJSON
// ---------------------------------------------------------------------------

TEST_F(WebAuthnCryptoTest, ParseClientData_ExtractsRequiredFields)
{
    std::string error;
    const auto parsed = parseClientDataJSON(kClientDataJson, &error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_EQ(parsed->type, "webauthn.create");
    // The challenge's base64url text is kept verbatim -- the caller compares
    // it against the stored form (e.g. a wrong challenge must be visible
    // here, not silently decoded/re-encoded).
    EXPECT_EQ(parsed->challenge, kChallenge);
    EXPECT_EQ(parsed->origin, kOrigin);
    EXPECT_FALSE(parsed->crossOriginPresent);
    EXPECT_FALSE(parsed->tokenBindingPresent);
}

TEST_F(WebAuthnCryptoTest, ParseClientData_CrossOriginVariants)
{
    const auto withFalse = parseClientDataJSON(
        "{\"type\":\"webauthn.get\",\"challenge\":\"Y2hhbGxlbmdl\",\"origin\":\"https://example.com\",\"crossOrigin\":false}");
    ASSERT_TRUE(withFalse.has_value());
    EXPECT_TRUE(withFalse->crossOriginPresent);
    EXPECT_FALSE(withFalse->crossOrigin);

    const auto withTrue = parseClientDataJSON(
        "{\"type\":\"webauthn.get\",\"challenge\":\"Y2hhbGxlbmdl\",\"origin\":\"https://other.example.net\",\"crossOrigin\":true}");
    ASSERT_TRUE(withTrue.has_value());
    EXPECT_TRUE(withTrue->crossOriginPresent);
    EXPECT_TRUE(withTrue->crossOrigin);

    // crossOrigin present but not a boolean is rejected.
    EXPECT_FALSE(parseClientDataJSON(
                     "{\"type\":\"webauthn.get\",\"challenge\":\"c\",\"origin\":\"https://example.com\",\"crossOrigin\":\"yes\"}")
                     .has_value());
}

TEST_F(WebAuthnCryptoTest, ParseClientData_TokenBindingSupportedAndNotSupported)
{
    const auto supported = parseClientDataJSON(
        "{\"type\":\"webauthn.create\",\"challenge\":\"c\",\"origin\":\"https://example.com\",\"tokenBinding\":{\"status\":\"supported\"}}");
    ASSERT_TRUE(supported.has_value());
    EXPECT_TRUE(supported->tokenBindingPresent);
    EXPECT_EQ(supported->tokenBindingStatus, "supported");

    const auto notSupported = parseClientDataJSON(
        "{\"type\":\"webauthn.create\",\"challenge\":\"c\",\"origin\":\"https://example.com\",\"tokenBinding\":{\"status\":\"not-supported\"}}");
    ASSERT_TRUE(notSupported.has_value());
    EXPECT_EQ(notSupported->tokenBindingStatus, "not-supported");
}

TEST_F(WebAuthnCryptoTest, ParseClientData_TokenBindingPresentIsRejected)
{
    // L2 §5.8.10: "present" means the client negotiated token binding with
    // this server; since it never does, this is a hard parse failure.
    std::string error;
    const auto parsed = parseClientDataJSON(
        "{\"type\":\"webauthn.create\",\"challenge\":\"c\",\"origin\":\"https://example.com\",\"tokenBinding\":{\"status\":\"present\"}}",
        &error);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_NE(error.find("present"), std::string::npos);
}

TEST_F(WebAuthnCryptoTest, ParseClientData_TokenBindingMalformedIsRejected)
{
    const std::string prefix = "{\"type\":\"webauthn.create\",\"challenge\":\"c\",\"origin\":\"https://example.com\",";
    EXPECT_FALSE(parseClientDataJSON(prefix + "\"tokenBinding\":{\"status\":\"bogus\"}}").has_value());
    EXPECT_FALSE(parseClientDataJSON(prefix + "\"tokenBinding\":{\"status\":42}}").has_value());
    EXPECT_FALSE(parseClientDataJSON(prefix + "\"tokenBinding\":\"supported\"}").has_value());
}

TEST_F(WebAuthnCryptoTest, ParseClientData_MissingOrWronglyTypedRequiredFields)
{
    EXPECT_FALSE(parseClientDataJSON("{\"challenge\":\"c\",\"origin\":\"https://example.com\"}").has_value());
    EXPECT_FALSE(parseClientDataJSON("{\"type\":\"webauthn.create\",\"origin\":\"https://example.com\"}").has_value());
    EXPECT_FALSE(parseClientDataJSON("{\"type\":\"webauthn.create\",\"challenge\":\"c\"}").has_value());
    EXPECT_FALSE(
        parseClientDataJSON("{\"type\":42,\"challenge\":\"c\",\"origin\":\"https://example.com\"}").has_value());
    EXPECT_FALSE(
        parseClientDataJSON("{\"type\":\"webauthn.create\",\"challenge\":42,\"origin\":\"https://example.com\"}")
            .has_value());
    EXPECT_FALSE(
        parseClientDataJSON("{\"type\":\"webauthn.create\",\"challenge\":\"c\",\"origin\":[\"https://example.com\"]}")
            .has_value());
}

TEST_F(WebAuthnCryptoTest, ParseClientData_InvalidJsonIsRejected)
{
    std::string error;
    EXPECT_FALSE(parseClientDataJSON("", &error).has_value());
    EXPECT_FALSE(parseClientDataJSON("not json", &error).has_value());
    EXPECT_FALSE(parseClientDataJSON("{\"type\":\"webauthn.create\"", &error).has_value());  // truncated
    // Trailing tokens after the object are rejected (strict reader).
    EXPECT_FALSE(parseClientDataJSON(kClientDataJson + " garbage", &error).has_value());
    // Duplicate keys are rejected rather than last-wins.
    EXPECT_FALSE(parseClientDataJSON(
                     "{\"type\":\"webauthn.create\",\"challenge\":\"c\",\"origin\":\"https://example.com\",\"origin\":\"https://evil.example\"}",
                     &error)
                     .has_value());
}

// ---------------------------------------------------------------------------
// parseAttestationObject
// ---------------------------------------------------------------------------

TEST_F(WebAuthnCryptoTest, ParseAttestationObject_FmtNoneShape)
{
    const std::string authData = registrationAuthData();
    const std::string attestationObject = buildAttestationObject(authData, "none", true);

    std::string error;
    const auto parsed = parseAttestationObject(attestationObject, &error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_EQ(parsed->fmt, "none");
    EXPECT_TRUE(parsed->attStmtEmptyMap);
    EXPECT_EQ(parsed->authData, authData);
}

TEST_F(WebAuthnCryptoTest, ParseAttestationObject_NonNoneFmtRecordedVerbatim)
{
    // Whether an attestation statement format is acceptable is the service
    // layer's policy call; the parser must faithfully report it (including
    // a non-empty attStmt map).
    const std::string authData = registrationAuthData();
    const auto parsed = parseAttestationObject(buildAttestationObject(authData, "packed", false));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->fmt, "packed");
    EXPECT_FALSE(parsed->attStmtEmptyMap);
    EXPECT_EQ(parsed->authData, authData);
}

TEST_F(WebAuthnCryptoTest, ParseAttestationObject_RejectsMalformedShapes)
{
    const std::string authData = registrationAuthData();
    std::string error;

    // authData as a text string instead of a byte string.
    std::string badAuthDataType;
    badAuthDataType.push_back('\xa3');
    appendText(badAuthDataType, "fmt");
    appendText(badAuthDataType, "none");
    appendText(badAuthDataType, "attStmt");
    badAuthDataType.push_back('\xa0');
    appendText(badAuthDataType, "authData");
    appendText(badAuthDataType, authData);  // WRONG: text, not bytes
    EXPECT_FALSE(parseAttestationObject(badAuthDataType, &error).has_value());
    EXPECT_NE(error.find("authData"), std::string::npos);

    // Missing fmt.
    std::string missingFmt;
    missingFmt.push_back('\xa2');
    appendText(missingFmt, "attStmt");
    missingFmt.push_back('\xa0');
    appendText(missingFmt, "authData");
    appendBytes(missingFmt, authData);
    EXPECT_FALSE(parseAttestationObject(missingFmt, &error).has_value());

    // attStmt as an integer.
    std::string badAttStmt;
    badAttStmt.push_back('\xa3');
    appendText(badAttStmt, "fmt");
    appendText(badAttStmt, "none");
    appendText(badAttStmt, "attStmt");
    appendUint(badAttStmt, 42);
    appendText(badAttStmt, "authData");
    appendBytes(badAttStmt, authData);
    EXPECT_FALSE(parseAttestationObject(badAttStmt, &error).has_value());

    // Root not a map.
    EXPECT_FALSE(parseAttestationObject(std::string("\x01", 1), &error).has_value());
    // Not CBOR at all.
    EXPECT_FALSE(parseAttestationObject("\xff\xfe", &error).has_value());
}

// ---------------------------------------------------------------------------
// parseAuthData
// ---------------------------------------------------------------------------

TEST_F(WebAuthnCryptoTest, ParseAuthData_RegistrationShapeRoundTrip)
{
    const std::string authData = registrationAuthData();  // UP|UV|AT

    std::string error;
    const auto parsed = parseAuthData(authData, true, &error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_EQ(parsed->rpIdHash, sha256(kRpId));
    EXPECT_NE(parsed->rpIdHash, sha256("evil.example"));  // wrong-RP comparison is caller-side but must be possible
    EXPECT_TRUE(parsed->up);
    EXPECT_TRUE(parsed->uv);
    EXPECT_TRUE(parsed->at);
    EXPECT_FALSE(parsed->ed);
    EXPECT_EQ(parsed->signCount, kSignCount);
    EXPECT_EQ(parsed->aaguid, kAaguid);
    EXPECT_EQ(parsed->credentialId, kCredentialId);
    // The COSE key bytes must be exactly the embedded item, no more no less.
    EXPECT_EQ(parsed->coseKey, coseKey_);
}

TEST_F(WebAuthnCryptoTest, ParseAuthData_FlagBitsDecodeIndependently)
{
    const auto withUserPresent = parseAuthData(registrationAuthData(0x41 /*UP|AT*/), true);
    ASSERT_TRUE(withUserPresent.has_value());
    EXPECT_TRUE(withUserPresent->up);
    EXPECT_FALSE(withUserPresent->uv);  // UV=0

    const auto withUserVerifiedOnly = parseAuthData(registrationAuthData(0x44 /*UV|AT, UP=0*/), true);
    ASSERT_TRUE(withUserVerifiedOnly.has_value());
    EXPECT_FALSE(withUserVerifiedOnly->up);  // UP=0 -- the service layer rejects this, parsing must still surface it
    EXPECT_TRUE(withUserVerifiedOnly->uv);

    const auto noUserFlags = parseAuthData(registrationAuthData(0x40 /*AT only*/), true);
    ASSERT_TRUE(noUserFlags.has_value());
    EXPECT_FALSE(noUserFlags->up);
    EXPECT_FALSE(noUserFlags->uv);
}

TEST_F(WebAuthnCryptoTest, ParseAuthData_SignCountIsBigEndian)
{
    AuthDataSpec spec;
    spec.rpIdHash = sha256(kRpId);
    spec.flags = 0x01;  // UP only, assertion shape
    spec.signCount = 0xDEADBEEFu;
    const auto parsed = parseAuthData(buildAuthData(spec), false);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->signCount, 0xDEADBEEFu);  // 3735928559
}

TEST_F(WebAuthnCryptoTest, ParseAuthData_AssertionShapeWithoutAttestedData)
{
    // 37-byte assertion authData: UP only.
    AuthDataSpec spec;
    spec.rpIdHash = sha256(kRpId);
    spec.flags = 0x01;
    spec.signCount = 7;
    const std::string authData = buildAuthData(spec);
    ASSERT_EQ(authData.size(), 37u);

    std::string error;
    const auto parsed = parseAuthData(authData, false, &error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_TRUE(parsed->up);
    EXPECT_FALSE(parsed->at);
    EXPECT_TRUE(parsed->aaguid.empty());
    EXPECT_TRUE(parsed->credentialId.empty());

    // The same buffer cannot satisfy a registration-shaped request.
    EXPECT_FALSE(parseAuthData(authData, true, &error).has_value());
    EXPECT_NE(error.find("AT flag"), std::string::npos);
}

TEST_F(WebAuthnCryptoTest, ParseAuthData_AssertionShapeStillParsesAttestedData)
{
    // An authenticator MAY include attested credential data in an
    // assertion; requireAttestedCredentialData=false must still walk past
    // it (and report the fields).
    const std::string authData = registrationAuthData();  // AT flag set
    const auto parsed = parseAuthData(authData, false);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->at);
    EXPECT_EQ(parsed->credentialId, kCredentialId);
    EXPECT_EQ(parsed->coseKey, coseKey_);
}

TEST_F(WebAuthnCryptoTest, ParseAuthData_TruncatedBuffersRejected)
{
    const std::string authData = registrationAuthData();
    std::string error;

    // Fixed part one byte short.
    EXPECT_FALSE(parseAuthData(authData.substr(0, 36), true, &error).has_value());

    // AT section present but COSE key omitted entirely.
    AuthDataSpec spec;
    spec.rpIdHash = sha256(kRpId);
    spec.flags = 0x45;
    spec.signCount = kSignCount;
    spec.aaguid = kAaguid;
    spec.credentialId = kCredentialId;
    // spec.coseKey left empty
    EXPECT_FALSE(parseAuthData(buildAuthData(spec), true, &error).has_value());

    // One trailing byte after the COSE key.
    EXPECT_FALSE(parseAuthData(authData + std::string(1, '\x00'), true, &error).has_value());
    EXPECT_NE(error.find("trailing"), std::string::npos);
}

TEST_F(WebAuthnCryptoTest, ParseAuthData_CredentialIdOver1023BytesRejected)
{
    AuthDataSpec spec;
    spec.rpIdHash = sha256(kRpId);
    spec.flags = 0x45;
    spec.signCount = 0;
    spec.aaguid = kAaguid;
    // buildAuthData derives the length field from the payload size, so a
    // 1024-byte credentialId yields a 0x0400 length field (limit: 1023) with
    // the payload actually present -- only the cap can reject it.
    spec.credentialId = std::string(1024, 'c');

    std::string error;
    EXPECT_FALSE(parseAuthData(buildAuthData(spec), true, &error).has_value());
    EXPECT_NE(error.find("1023"), std::string::npos);

    // 1023 bytes is fine.
    spec.credentialId = std::string(1023, 'c');
    spec.coseKey = coseKey_;
    EXPECT_TRUE(parseAuthData(buildAuthData(spec), true, &error).has_value()) << error;
}

TEST_F(WebAuthnCryptoTest, ParseAuthData_ExtensionDataAcceptedAndGarbageRejected)
{
    std::string error;

    // ED flag + a well-formed (empty map) extension block.
    {
        AuthDataSpec spec;
        spec.rpIdHash = sha256(kRpId);
        spec.flags = 0x45 | 0x80;  // UP|UV|AT|ED
        spec.signCount = kSignCount;
        spec.aaguid = kAaguid;
        spec.credentialId = kCredentialId;
        spec.coseKey = coseKey_;
        spec.extension = std::string(1, '\xa0');  // CBOR empty map
        const auto parsed = parseAuthData(buildAuthData(spec), true, &error);
        ASSERT_TRUE(parsed.has_value()) << error;
        EXPECT_TRUE(parsed->ed);
    }

    // ED flag + valid non-empty extension (map with one uint entry).
    {
        AuthDataSpec spec;
        spec.rpIdHash = sha256(kRpId);
        spec.flags = 0x45 | 0x80;
        spec.signCount = kSignCount;
        spec.aaguid = kAaguid;
        spec.credentialId = kCredentialId;
        spec.coseKey = coseKey_;
        std::string extension;
        extension.push_back('\xa1');
        appendText(extension, "credProtect");
        appendUint(extension, 2);
        spec.extension = extension;
        const auto parsed = parseAuthData(buildAuthData(spec), true, &error);
        ASSERT_TRUE(parsed.has_value()) << error;
        EXPECT_TRUE(parsed->ed);
    }

    // ED flag + garbage: the extension cannot even be measured -> fail.
    {
        AuthDataSpec spec;
        spec.rpIdHash = sha256(kRpId);
        spec.flags = 0x45 | 0x80;
        spec.signCount = kSignCount;
        spec.aaguid = kAaguid;
        spec.credentialId = kCredentialId;
        spec.coseKey = coseKey_;
        spec.extension = "\xff\xfe";  // break codes, not a CBOR item
        EXPECT_FALSE(parseAuthData(buildAuthData(spec), true, &error).has_value());
        EXPECT_NE(error.find("extension"), std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// parseCoseKeyEs256
// ---------------------------------------------------------------------------

TEST_F(WebAuthnCryptoTest, ParseCoseKey_HappyPath)
{
    std::string error;
    const auto parsed = parseCoseKeyEs256(coseKey_, &error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_EQ(parsed->x, x_);
    EXPECT_EQ(parsed->y, y_);
}

TEST_F(WebAuthnCryptoTest, ParseCoseKey_WrongAlgorithmRejected)
{
    std::string error;
    // alg -257 (RS256) -- the error must name the field.
    const auto rs256 = parseCoseKeyEs256(buildCoseKey(2, -257, 1, x_, y_), &error);
    EXPECT_FALSE(rs256.has_value());
    EXPECT_NE(error.find("alg"), std::string::npos);
}

TEST_F(WebAuthnCryptoTest, ParseCoseKey_WrongKeyTypeRejected)
{
    std::string error;
    // kty 1 (OKP) instead of 2 (EC2).
    EXPECT_FALSE(parseCoseKeyEs256(buildCoseKey(1, -7, 1, x_, y_), &error).has_value());
    EXPECT_NE(error.find("kty"), std::string::npos);
}

TEST_F(WebAuthnCryptoTest, ParseCoseKey_WrongCurveRejected)
{
    std::string error;
    // kty 2 but crv 2 (Ed25519's curve identifier, not P-256's 1).
    EXPECT_FALSE(parseCoseKeyEs256(buildCoseKey(2, -7, 2, x_, y_), &error).has_value());
    EXPECT_NE(error.find("crv"), std::string::npos);
}

TEST_F(WebAuthnCryptoTest, ParseCoseKey_ShortCoordinatesRejected)
{
    std::string error;
    const std::string shortX(31, 'x');
    const std::string shortY(16, 'y');
    EXPECT_FALSE(parseCoseKeyEs256(buildCoseKey(2, -7, 1, shortX, y_), &error).has_value());
    EXPECT_NE(error.find("x"), std::string::npos);
    EXPECT_FALSE(parseCoseKeyEs256(buildCoseKey(2, -7, 1, x_, shortY), &error).has_value());
    EXPECT_NE(error.find("y"), std::string::npos);
}

TEST_F(WebAuthnCryptoTest, ParseCoseKey_MissingFieldAndNonCborRejected)
{
    std::string error;
    // map(4) with alg dropped entirely.
    std::string noAlg;
    noAlg.push_back('\xa4');
    appendInt(noAlg, 1);
    appendInt(noAlg, 2);
    appendInt(noAlg, -1);
    appendInt(noAlg, 1);
    appendInt(noAlg, -2);
    appendBytes(noAlg, x_);
    appendInt(noAlg, -3);
    appendBytes(noAlg, y_);
    EXPECT_FALSE(parseCoseKeyEs256(noAlg, &error).has_value());
    EXPECT_NE(error.find("alg"), std::string::npos);

    // Empty / garbage inputs.
    EXPECT_FALSE(parseCoseKeyEs256("", &error).has_value());
    EXPECT_FALSE(parseCoseKeyEs256("\xff\xff\xff\xff", &error).has_value());
}

// ---------------------------------------------------------------------------
// verifyEs256Signature
// ---------------------------------------------------------------------------

TEST_F(WebAuthnCryptoTest, VerifyEs256_AcceptsGenuineSignature)
{
    const std::string authData = registrationAuthData();
    const std::string signature = signEs256(key_.get(), signedMessage(authData, kClientDataJson));
    ASSERT_FALSE(signature.empty());

    std::string error;
    const auto key = parseCoseKeyEs256(coseKey_, &error);
    ASSERT_TRUE(key.has_value()) << error;
    EXPECT_TRUE(verifyEs256Signature(*key, authData, kClientDataJson, signature));
}

TEST_F(WebAuthnCryptoTest, VerifyEs256_RejectsTamperedSignature)
{
    const std::string authData = registrationAuthData();
    std::string signature = signEs256(key_.get(), signedMessage(authData, kClientDataJson));
    ASSERT_GE(signature.size(), 8u);

    // Flip one byte in the middle of the DER blob.
    signature[signature.size() / 2] = static_cast<char>(signature[signature.size() / 2] ^ 0x40);
    const auto key = parseCoseKeyEs256(coseKey_);
    ASSERT_TRUE(key.has_value());
    EXPECT_FALSE(verifyEs256Signature(*key, authData, kClientDataJson, signature));
}

TEST_F(WebAuthnCryptoTest, VerifyEs256_RejectsTamperedAuthData)
{
    const std::string authData = registrationAuthData();
    const std::string signature = signEs256(key_.get(), signedMessage(authData, kClientDataJson));

    // Corrupt the first rpIdHash byte (the "wrong RP" case): signature no
    // longer matches the signed message.
    std::string wrongRp = authData;
    wrongRp[0] = static_cast<char>(wrongRp[0] ^ 0x01);
    const auto key = parseCoseKeyEs256(coseKey_);
    ASSERT_TRUE(key.has_value());
    EXPECT_FALSE(verifyEs256Signature(*key, wrongRp, kClientDataJson, signature));

    // Corrupt a signCount byte instead -- same outcome.
    std::string wrongCount = authData;
    wrongCount[36] = static_cast<char>(wrongCount[36] ^ 0x01);
    EXPECT_FALSE(verifyEs256Signature(*key, wrongCount, kClientDataJson, signature));
}

TEST_F(WebAuthnCryptoTest, VerifyEs256_RejectsDifferentClientData)
{
    const std::string authData = registrationAuthData();
    const std::string signature = signEs256(key_.get(), signedMessage(authData, kClientDataJson));

    // A different challenge (the replayed/wrong-challenge case) hashes to a
    // different clientDataJSON digest, so the signature must fail.
    const std::string otherClientData =
        "{\"type\":\"webauthn.create\",\"challenge\":\"REPLAYED-CHALLENGE\",\"origin\":\"https://example.com\"}";
    const auto key = parseCoseKeyEs256(coseKey_);
    ASSERT_TRUE(key.has_value());
    EXPECT_FALSE(verifyEs256Signature(*key, authData, otherClientData, signature));
}

TEST_F(WebAuthnCryptoTest, VerifyEs256_RejectsWrongKey)
{
    const std::string authData = registrationAuthData();
    const std::string signature = signEs256(key_.get(), signedMessage(authData, kClientDataJson));

    // Verify against a different P-256 keypair's coordinates.
    const EvpPkeyPtr otherKey = generateP256();
    ASSERT_TRUE(otherKey != nullptr);
    unsigned char point[65] = {0};
    size_t pointLength = 0;
    ASSERT_EQ(EVP_PKEY_get_octet_string_param(otherKey.get(), OSSL_PKEY_PARAM_PUB_KEY, point, sizeof(point),
                                             &pointLength),
              1);
    fulla::identity::webauthn::Es256PublicKey other;
    other.x.assign(reinterpret_cast<const char *>(point + 1), 32);
    other.y.assign(reinterpret_cast<const char *>(point + 33), 32);

    EXPECT_FALSE(verifyEs256Signature(other, authData, kClientDataJson, signature));
}

TEST_F(WebAuthnCryptoTest, VerifyEs256_RejectsMalformedSignatures)
{
    const std::string authData = registrationAuthData();
    const auto key = parseCoseKeyEs256(coseKey_);
    ASSERT_TRUE(key.has_value());

    EXPECT_FALSE(verifyEs256Signature(*key, authData, kClientDataJson, ""));        // empty
    EXPECT_FALSE(verifyEs256Signature(*key, authData, kClientDataJson, "junk"));    // not DER
    // Valid DER wrapper but nonsense ECDSA content.
    EXPECT_FALSE(verifyEs256Signature(*key, authData, kClientDataJson, std::string("\x30\x03\x02\x01\x01", 5)));

    // Structurally wrong keys never reach OpenSSL logic.
    const fulla::identity::webauthn::Es256PublicKey shortKey{std::string(31, 'x'), std::string(31, 'y')};
    EXPECT_FALSE(verifyEs256Signature(shortKey, authData, kClientDataJson, std::string("\x30\x02\x02\x01", 4)));
}

TEST_F(WebAuthnCryptoTest, VerifyEs256_RejectsPointNotOnCurve)
{
    // Mangle one coordinate so (x, y) is no longer a curve point; the EVP
    // key import must reject it rather than verify anything.
    const std::string authData = registrationAuthData();
    const std::string signature = signEs256(key_.get(), signedMessage(authData, kClientDataJson));
    fulla::identity::webauthn::Es256PublicKey offCurve;
    offCurve.x = x_;
    offCurve.x[31] = static_cast<char>(offCurve.x[31] ^ 0xFF);
    offCurve.y = y_;
    EXPECT_FALSE(verifyEs256Signature(offCurve, authData, kClientDataJson, signature));
}

// ---------------------------------------------------------------------------
// End-to-end: the crypto half of a "none"-attestation registration
// (L2 §7.1 parsing steps + step 19 verification).
// ---------------------------------------------------------------------------

TEST_F(WebAuthnCryptoTest, EndToEnd_RegistrationCeremonyCryptoSteps)
{
    const std::string authData = registrationAuthData();
    const std::string clientDataJson = kClientDataJson;
    const std::string signature = signEs256(key_.get(), signedMessage(authData, clientDataJson));
    const std::string attestationObject = buildAttestationObject(authData, "none", true);

    std::string error;

    // Step: clientDataJSON.
    const auto clientData = parseClientDataJSON(clientDataJson, &error);
    ASSERT_TRUE(clientData.has_value()) << error;
    EXPECT_EQ(clientData->challenge, kChallenge);  // caller compares against the stored challenge

    // Step: attestationObject.
    const auto attestation = parseAttestationObject(attestationObject, &error);
    ASSERT_TRUE(attestation.has_value()) << error;
    EXPECT_EQ(attestation->fmt, "none");
    EXPECT_TRUE(attestation->attStmtEmptyMap);

    // Step: authData.
    const auto parsed = parseAuthData(attestation->authData, true, &error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_EQ(parsed->rpIdHash, sha256(kRpId));
    EXPECT_TRUE(parsed->up);
    EXPECT_EQ(parsed->signCount, kSignCount);

    // Step: credential public key.
    const auto key = parseCoseKeyEs256(parsed->coseKey, &error);
    ASSERT_TRUE(key.has_value()) << error;

    // Step: signature over authData || SHA-256(clientDataJSON).
    EXPECT_TRUE(verifyEs256Signature(*key, attestation->authData, clientDataJson, signature));
}
