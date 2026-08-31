// #142 (WebAuthn real-signature verification): implementation of the pure
// WebAuthn L2 parsing/verification primitives declared in WebAuthnCrypto.h.
// See that header for the ceremony-step mapping and the no-throw contract.

#include "WebAuthnCrypto.h"

#include "CborReader.h"

#include <json/json.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/sha.h>

#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace fulla::identity::webauthn
{

namespace
{

const char *const kTokenBindingStatuses[] = {"present", "supported", "not-supported"};

// COSE key labels used by the ES256 layout (RFC 9053 §7.1.1 + §13.1.1's
// crv table for P-256); keys 1/-1/-2/-3 are negative-or-positive CBOR
// integers, hence int64_t here.
constexpr int64_t kCoseLabelKty = 1;   // key type
constexpr int64_t kCoseLabelAlg = 3;   // algorithm
constexpr int64_t kCoseLabelCrv = -1;  // curve
constexpr int64_t kCoseLabelX = -2;
constexpr int64_t kCoseLabelY = -3;

constexpr int64_t kCoseKtyEc2 = 2;   // Elliptic Curve Keys w/ x- and y-coordinate
constexpr int64_t kCoseAlgEs256 = -7;
constexpr int64_t kCoseCrvP256 = 1;

// L2 §6.4 caps credential IDs at 1023 bytes.
constexpr size_t kMaxCredentialIdBytes = 1023;

// Sets *errorOut (when non-null) and returns a default-constructed nullopt
// of the caller's optional -- used as `return fail<T>(...)`.
template <typename T>
std::optional<T> fail(std::string *errorOut, std::string reason)
{
    if (errorOut != nullptr)
        *errorOut = std::move(reason);
    return std::nullopt;
}

// OpenSSL EVP RAII wrappers (unique_ptr keeps every early return below
// leak-free without goto cleanup ladders).
struct EVPMdCtxDeleter
{
    void operator()(EVP_MD_CTX *ctx) const
    {
        EVP_MD_CTX_free(ctx);
    }
};
struct EvpPkeyDeleter
{
    void operator()(EVP_PKEY *key) const
    {
        EVP_PKEY_free(key);
    }
};
struct EvpPkeyCtxDeleter
{
    void operator()(EVP_PKEY_CTX *ctx) const
    {
        EVP_PKEY_CTX_free(ctx);
    }
};
struct OsslParamDeleter
{
    void operator()(OSSL_PARAM *params) const
    {
        OSSL_PARAM_free(params);
    }
};
struct OsslParamBldDeleter
{
    void operator()(OSSL_PARAM_BLD *bld) const
    {
        OSSL_PARAM_BLD_free(bld);
    }
};

// SHA-256 of `data` as a 32-byte binary std::string; nullopt on the (never
// observed in practice) OpenSSL failure.
std::optional<std::string> sha256(const std::string &data)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    unsigned int digestLen = 0;
    if (EVP_Digest(data.data(), data.size(), digest, &digestLen, EVP_sha256(), nullptr) != 1 ||
        digestLen != SHA256_DIGEST_LENGTH)
        return std::nullopt;
    return std::string(reinterpret_cast<const char *>(digest), digestLen);
}

}  // namespace

// ---------------------------------------------------------------------------
// clientDataJSON
// ---------------------------------------------------------------------------
std::optional<ParsedClientData> parseClientDataJSON(const std::string &clientDataJSON, std::string *errorOut)
{
    try
    {
        // Strict JSON: clientDataJSON is attacker-controlled single-object
        // JSON, so trailing tokens and duplicate keys are rejected instead
        // of silently resolved (jsoncpp would otherwise accept both).
        Json::CharReaderBuilder builder;
        builder["collectComments"] = false;
        builder["failIfExtra"] = true;
        builder["rejectDupKeys"] = true;

        Json::Value root;
        std::string parseErrors;
        const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        if (!reader->parse(clientDataJSON.data(), clientDataJSON.data() + clientDataJSON.size(), &root, &parseErrors))
            return fail<ParsedClientData>(errorOut, "invalid JSON: " + parseErrors);
        if (!root.isObject())
            return fail<ParsedClientData>(errorOut, "clientDataJSON must be a JSON object");

        // L2 §5.8.1: type/challenge/origin are REQUIRED and strings; the
        // challenge's base64url value is kept verbatim for the caller to
        // compare against its stored challenge form.
        for (const char *field : {"type", "challenge", "origin"})
        {
            if (!root.isMember(field) || !root[field].isString())
                return fail<ParsedClientData>(errorOut, std::string("clientDataJSON.") + field +
                                                                " must be present and a string");
        }

        ParsedClientData parsed;
        parsed.type = root["type"].asString();
        parsed.challenge = root["challenge"].asString();
        parsed.origin = root["origin"].asString();

        if (root.isMember("crossOrigin"))
        {
            if (!root["crossOrigin"].isBool())
                return fail<ParsedClientData>(errorOut, "clientDataJSON.crossOrigin must be a boolean");
            parsed.crossOriginPresent = true;
            parsed.crossOrigin = root["crossOrigin"].asBool();
        }

        if (root.isMember("tokenBinding"))
        {
            const Json::Value &tokenBinding = root["tokenBinding"];
            if (!tokenBinding.isObject() || !tokenBinding.isMember("status") ||
                !tokenBinding["status"].isString())
                return fail<ParsedClientData>(errorOut,
                                              "clientDataJSON.tokenBinding must be an object with a string status");
            parsed.tokenBindingPresent = true;
            parsed.tokenBindingStatus = tokenBinding["status"].asString();

            bool knownStatus = false;
            for (const char *status : kTokenBindingStatuses)
                knownStatus = knownStatus || (parsed.tokenBindingStatus == status);
            if (!knownStatus)
                return fail<ParsedClientData>(errorOut,
                                              "clientDataJSON.tokenBinding.status is not a recognized value");
            // L2 §5.8.10 / §7.1 step 9: "present" would mean the client
            // expects this server to have negotiated token binding, which
            // it never does -- that is a verification failure, not a skip.
            if (parsed.tokenBindingStatus == "present")
                return fail<ParsedClientData>(errorOut,
                                              "tokenBinding.status \"present\" is invalid (token binding is not "
                                              "negotiated by this server)");
        }

        return parsed;
    }
    catch (const std::exception &e)
    {
        return fail<ParsedClientData>(errorOut, std::string("unexpected exception: ") + e.what());
    }
    catch (...)
    {
        return fail<ParsedClientData>(errorOut, "unexpected non-standard exception");
    }
}

// ---------------------------------------------------------------------------
// attestationObject
// ---------------------------------------------------------------------------
std::optional<ParsedAttestationObject> parseAttestationObject(const std::string &attestationObject,
                                                              std::string *errorOut)
{
    try
    {
        const std::optional<CborReader> reader = CborReader::load(attestationObject, errorOut);
        if (!reader.has_value())
            return std::nullopt;  // errorOut already describes the failure

        const CborNode root = reader->root();
        if (!root.isMap())
            return fail<ParsedAttestationObject>(errorOut, "attestationObject must be a CBOR map");

        const std::optional<CborNode> fmt = root.mapLookup("fmt");
        if (!fmt.has_value() || !fmt->isString())
            return fail<ParsedAttestationObject>(errorOut, "attestationObject.fmt must be a text string");

        const std::optional<CborNode> attStmt = root.mapLookup("attStmt");
        if (!attStmt.has_value() || !attStmt->isMap())
            return fail<ParsedAttestationObject>(errorOut, "attestationObject.attStmt must be a CBOR map");

        const std::optional<CborNode> authData = root.mapLookup("authData");
        if (!authData.has_value() || !authData->isBytes())
            return fail<ParsedAttestationObject>(errorOut, "attestationObject.authData must be a byte string");

        ParsedAttestationObject parsed;
        parsed.fmt = *fmt->asString();
        parsed.attStmtEmptyMap = (*attStmt->mapSize() == 0);
        parsed.authData = *authData->asBytes();
        return parsed;
    }
    catch (const std::exception &e)
    {
        return fail<ParsedAttestationObject>(errorOut, std::string("unexpected exception: ") + e.what());
    }
    catch (...)
    {
        return fail<ParsedAttestationObject>(errorOut, "unexpected non-standard exception");
    }
}

// ---------------------------------------------------------------------------
// authenticatorData
// ---------------------------------------------------------------------------
std::optional<ParsedAuthData> parseAuthData(const std::string &authData,
                                            bool requireAttestedCredentialData,
                                            std::string *errorOut)
{
    const auto measureAt = [&authData, errorOut](size_t offset, const char *what) -> std::optional<size_t> {
        std::string reason;
        std::optional<size_t> length = CborReader::measure(authData.data() + offset, authData.size() - offset, &reason);
        if (!length.has_value() && errorOut != nullptr)
            *errorOut = std::string(what) + ": " + reason;
        return length;
    };

    try
    {
        // rpIdHash(32) + flags(1) + signCount(4)
        constexpr size_t kFixedPartBytes = 37;
        if (authData.size() < kFixedPartBytes)
            return fail<ParsedAuthData>(errorOut, "authData shorter than the 37-byte fixed part");

        const unsigned char *bytes = reinterpret_cast<const unsigned char *>(authData.data());

        ParsedAuthData parsed;
        parsed.rpIdHash = authData.substr(0, 32);
        const unsigned char flags = bytes[32];
        parsed.up = (flags & 0x01) != 0;  // User Present
        parsed.uv = (flags & 0x04) != 0;  // User Verified
        parsed.at = (flags & 0x40) != 0;  // Attested credential data included
        parsed.ed = (flags & 0x80) != 0;  // Extension data included
        parsed.signCount = (static_cast<uint32_t>(bytes[33]) << 24) | (static_cast<uint32_t>(bytes[34]) << 16) |
                           (static_cast<uint32_t>(bytes[35]) << 8) | static_cast<uint32_t>(bytes[36]);

        size_t pos = kFixedPartBytes;
        if (parsed.at)
        {
            constexpr size_t kAaguidBytes = 16;
            if (authData.size() < pos + kAaguidBytes)
                return fail<ParsedAuthData>(errorOut, "authData: aaguid truncated");
            parsed.aaguid = authData.substr(pos, kAaguidBytes);
            pos += kAaguidBytes;

            if (authData.size() < pos + 2)
                return fail<ParsedAuthData>(errorOut, "authData: credentialIdLength truncated");
            const size_t credentialIdLength =
                (static_cast<size_t>(static_cast<unsigned char>(authData[pos])) << 8) |
                static_cast<size_t>(static_cast<unsigned char>(authData[pos + 1]));
            pos += 2;
            if (credentialIdLength > kMaxCredentialIdBytes)
                return fail<ParsedAuthData>(errorOut, "authData: credentialIdLength exceeds 1023 bytes");
            if (authData.size() < pos + credentialIdLength)
                return fail<ParsedAuthData>(errorOut, "authData: credentialId truncated");
            parsed.credentialId = authData.substr(pos, credentialIdLength);
            pos += credentialIdLength;

            // The COSE key is raw CBOR embedded without outer framing; its
            // extent can only be found by decoding it, under the same hard
            // limits CborReader applies everywhere.
            const std::optional<size_t> coseKeyLength = measureAt(pos, "authData: COSE key");
            if (!coseKeyLength.has_value())
                return std::nullopt;
            parsed.coseKey = authData.substr(pos, *coseKeyLength);
            pos += *coseKeyLength;
        }
        else if (requireAttestedCredentialData)
        {
            return fail<ParsedAuthData>(errorOut, "authData: AT flag not set but attested credential data is required");
        }

        if (parsed.ed)
        {
            // L2 §6.4: ED authenticator output must be a well-formed CBOR
            // item; if it cannot even be measured, the whole authData is
            // unusable rather than merely extension-less.
            const std::optional<size_t> extensionLength = measureAt(pos, "authData: extension data");
            if (!extensionLength.has_value())
                return std::nullopt;
            pos += *extensionLength;
        }

        if (pos != authData.size())
            return fail<ParsedAuthData>(errorOut, "authData: trailing bytes after the declared structure");
        return parsed;
    }
    catch (const std::exception &e)
    {
        return fail<ParsedAuthData>(errorOut, std::string("unexpected exception: ") + e.what());
    }
    catch (...)
    {
        return fail<ParsedAuthData>(errorOut, "unexpected non-standard exception");
    }
}

// ---------------------------------------------------------------------------
// COSE ES256 key
// ---------------------------------------------------------------------------
std::optional<Es256PublicKey> parseCoseKeyEs256(const std::string &coseKeyBytes, std::string *errorOut)
{
    try
    {
        const std::optional<CborReader> reader = CborReader::load(coseKeyBytes, errorOut);
        if (!reader.has_value())
            return std::nullopt;

        const CborNode root = reader->root();
        if (!root.isMap())
            return fail<Es256PublicKey>(errorOut, "COSE key must be a CBOR map");

        std::optional<int64_t> kty;
        std::optional<int64_t> alg;
        std::optional<int64_t> crv;
        std::optional<std::string> x;
        std::optional<std::string> y;

        const size_t pairCount = root.mapSize().value_or(0);
        for (size_t i = 0; i < pairCount; ++i)
        {
            const std::optional<std::pair<CborNode, CborNode>> pair = root.mapAt(i);
            if (!pair.has_value())
                return fail<Es256PublicKey>(errorOut, "COSE key: unreadable map entry");

            // COSE labels here are integers (1, 3, -1, -2, -3); entries
            // under other/non-integer labels (e.g. the optional "kid"
            // text label) are ignored per RFC 9053 §1.
            const std::optional<int64_t> label = pair->first.asInt();
            if (!label.has_value())
                continue;

            if (*label == kCoseLabelKty && pair->second.isInt())
                kty = pair->second.asInt();
            else if (*label == kCoseLabelAlg && pair->second.isInt())
                alg = pair->second.asInt();
            else if (*label == kCoseLabelCrv && pair->second.isInt())
                crv = pair->second.asInt();
            else if (*label == kCoseLabelX && pair->second.isBytes())
                x = pair->second.asBytes();
            else if (*label == kCoseLabelY && pair->second.isBytes())
                y = pair->second.asBytes();
        }

        if (!kty.has_value())
            return fail<Es256PublicKey>(errorOut, "COSE key: kty (label 1) missing or not an integer");
        if (*kty != kCoseKtyEc2)
            return fail<Es256PublicKey>(errorOut, "COSE key: kty (label 1) != 2 (EC2)");
        if (!alg.has_value())
            return fail<Es256PublicKey>(errorOut, "COSE key: alg (label 3) missing or not an integer");
        if (*alg != kCoseAlgEs256)
            return fail<Es256PublicKey>(errorOut, "COSE key: alg (label 3) != -7 (ES256)");
        if (!crv.has_value())
            return fail<Es256PublicKey>(errorOut, "COSE key: crv (label -1) missing or not an integer");
        if (*crv != kCoseCrvP256)
            return fail<Es256PublicKey>(errorOut, "COSE key: crv (label -1) != 1 (P-256)");
        if (!x.has_value())
            return fail<Es256PublicKey>(errorOut, "COSE key: x (label -2) missing or not a byte string");
        if (x->size() != 32)
            return fail<Es256PublicKey>(errorOut, "COSE key: x (label -2) is not 32 bytes");
        if (!y.has_value())
            return fail<Es256PublicKey>(errorOut, "COSE key: y (label -3) missing or not a byte string");
        if (y->size() != 32)
            return fail<Es256PublicKey>(errorOut, "COSE key: y (label -3) is not 32 bytes");

        Es256PublicKey parsed;
        parsed.x = std::move(*x);
        parsed.y = std::move(*y);
        return parsed;
    }
    catch (const std::exception &e)
    {
        return fail<Es256PublicKey>(errorOut, std::string("unexpected exception: ") + e.what());
    }
    catch (...)
    {
        return fail<Es256PublicKey>(errorOut, "unexpected non-standard exception");
    }
}

// ---------------------------------------------------------------------------
// ES256 signature verification
// ---------------------------------------------------------------------------
bool verifyEs256Signature(const Es256PublicKey &key,
                          const std::string &authData,
                          const std::string &clientDataJson,
                          const std::string &signatureDer)
{
    try
    {
        if (key.x.size() != 32 || key.y.size() != 32 || signatureDer.empty() || authData.size() < 37)
            return false;

        // Signed message per L2 §6.5.6 / §7.1 step 19: authData || SHA-256(
        // clientDataJSON) -- the hash, not the JSON text itself.
        const std::optional<std::string> clientDataHash = sha256(clientDataJson);
        if (!clientDataHash.has_value())
            return false;
        std::string message = authData;
        message += *clientDataHash;

        // Rebuild the P-256 public key from the affine coordinates: pack
        // the SEC1 uncompressed point 0x04 || x || y and import it through
        // the OpenSSL 3 EVP parameter interface (keeps us off the
        // EC_KEY/EC_POINT APIs deprecated since 3.0). fromdata() rejects
        // points not on the curve, which also rules out invalid keys.
        std::vector<unsigned char> point(65);
        point[0] = 0x04;
        std::memcpy(point.data() + 1, key.x.data(), 32);
        std::memcpy(point.data() + 33, key.y.data(), 32);

        const std::unique_ptr<OSSL_PARAM_BLD, OsslParamBldDeleter> paramBld(OSSL_PARAM_BLD_new());
        if (!paramBld ||
            OSSL_PARAM_BLD_push_utf8_string(paramBld.get(), OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0) != 1 ||
            OSSL_PARAM_BLD_push_octet_string(paramBld.get(), OSSL_PKEY_PARAM_PUB_KEY, point.data(), point.size()) != 1)
            return false;
        const std::unique_ptr<OSSL_PARAM, OsslParamDeleter> params(OSSL_PARAM_BLD_to_param(paramBld.get()));
        if (!params)
            return false;

        const std::unique_ptr<EVP_PKEY_CTX, EvpPkeyCtxDeleter> keyContext(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr));
        if (!keyContext || EVP_PKEY_fromdata_init(keyContext.get()) != 1)
            return false;
        EVP_PKEY *rawKey = nullptr;
        if (EVP_PKEY_fromdata(keyContext.get(), &rawKey, EVP_PKEY_PUBLIC_KEY, params.get()) != 1)
            return false;
        const std::unique_ptr<EVP_PKEY, EvpPkeyDeleter> publicKey(rawKey);

        // EVP_DigestVerify() is the one-shot form; ECDSA DER signatures
        // are exactly what authenticators emit for COSE alg -7 (L2 §6.5.6).
        const std::unique_ptr<EVP_MD_CTX, EVPMdCtxDeleter> mdContext(EVP_MD_CTX_new());
        if (!mdContext || EVP_DigestVerifyInit(mdContext.get(), nullptr, EVP_sha256(), nullptr, publicKey.get()) != 1)
            return false;
        return EVP_DigestVerify(mdContext.get(),
                                reinterpret_cast<const unsigned char *>(signatureDer.data()),
                                signatureDer.size(),
                                reinterpret_cast<const unsigned char *>(message.data()),
                                message.size()) == 1;
    }
    catch (...)
    {
        return false;
    }
}

}  // namespace fulla::identity::webauthn
