#pragma once

// #142 (WebAuthn real-signature verification): the pure parsing/verification
// primitives behind W3C WebAuthn Level 2 §7.1 (registration ceremony) and
// §6.1 (authentication ceremony). WebAuthnService decides policy (challenge
// match, origin allow-list, signCount monotonicity, RP ID hash equality);
// this header only turns raw client bytes into checked structures and
// verifies ES256 signatures. Everything here is a pure function:
//
//   - never throws (internal catch-all),
//   - parse failures return std::nullopt with an optional reason string in
//     *errorOut,
//   - verifyEs256Signature() returns a plain bool.
//
// Kept free of Drogon/jsoncpp-in-headers so the Domain layer stays portable
// (same rationale as TotpUtils.h); jsoncpp/OpenSSL/libcbor are used only in
// the .cc and linked PRIVATE by fulla-identity.

#include <cstdint>
#include <optional>
#include <string>

namespace fulla::identity::webauthn
{

// ---------------------------------------------------------------------------
// clientDataJSON (W3C WebAuthn L2 §5.8.1) -- UTF-8 JSON.
// ---------------------------------------------------------------------------
struct ParsedClientData
{
    std::string type;            // "webauthn.create" / "webauthn.get"
    std::string challenge;       // raw challenge as it appears in the JSON: the base64url string is NOT decoded here; callers compare against whatever form they stored
    std::string origin;
    bool crossOriginPresent = false;
    bool crossOrigin = false;
    bool tokenBindingPresent = false;
    std::string tokenBindingStatus;  // "present"|"supported"|"not-supported"
};

// Validates type/challenge/origin are present and strings. tokenBinding, when
// present, must carry one of the three L2 statuses and must NOT be "present"
// (L2 §5.8.10: a status of "present" means the client negotiated token
// binding with this server, which this server never does). Invalid or
// non-canonical JSON (trailing tokens, duplicate keys) -> nullopt.
std::optional<ParsedClientData> parseClientDataJSON(const std::string &clientDataJSON,
                                                    std::string *errorOut = nullptr);

// ---------------------------------------------------------------------------
// attestationObject (W3C WebAuthn L2 §6.5) -- CBOR bytes.
// ---------------------------------------------------------------------------
struct ParsedAttestationObject
{
    std::string fmt;  // e.g. "none", "packed" -- policy decision left to the caller
    std::string authData;
    bool attStmtEmptyMap = false;
};

std::optional<ParsedAttestationObject> parseAttestationObject(const std::string &attestationObject,
                                                              std::string *errorOut = nullptr);
// fmt must be a text string; attStmt must be a map (records whether it is the
// empty map that the "none" format mandates); authData must be a byte string.

// ---------------------------------------------------------------------------
// authenticatorData (W3C WebAuthn L2 §6.4) layout:
//
//   rpIdHash(32) flags(1) signCount(4, big-endian)
//   [AT: aaguid(16) credentialIdLength(2, BE, <= 1023) credentialId(n)
//        COSE key (raw CBOR, length recovered by decoding)]
//   [ED: extension authenticator output (raw CBOR, length recovered by
//        decoding; undecodable -> whole parse fails)]
//
// flags bits: 0=UP, 2=UV, 6=AT, 7=ED. Reserved bits are ignored.
// ---------------------------------------------------------------------------
struct ParsedAuthData
{
    std::string rpIdHash;  // 32 raw bytes (SHA-256 of the RP ID)
    bool up = false, uv = false, at = false, ed = false;
    uint32_t signCount = 0;  // big-endian as transmitted

    // Only meaningful when at == true:
    std::string aaguid;        // 16 raw bytes
    std::string credentialId;  // raw bytes
    std::string coseKey;       // raw CBOR bytes of the COSE key, exactly as encoded
};

// requireAttestedCredentialData=true is the registration shape (AT section
// mandatory); false is the assertion shape (AT section optional but still
// parsed/skipped if the authenticator set the flag). Truncated, oversized or
// trailing bytes -> nullopt.
std::optional<ParsedAuthData> parseAuthData(const std::string &authData,
                                            bool requireAttestedCredentialData,
                                            std::string *errorOut = nullptr);

// ---------------------------------------------------------------------------
// COSE ES256 key (RFC 9053 §7.1.1, P-256 / EC2):
// map{kty:2, alg:-7, crv:1, x:bstr32, y:bstr32}
// ---------------------------------------------------------------------------
struct Es256PublicKey
{
    std::string x, y;  // 32 raw bytes each
};

// kty != 2 / alg != -7 / crv != 1 / x or y not 32 bytes -> nullopt, with the
// offending COSE label named in *errorOut.
std::optional<Es256PublicKey> parseCoseKeyEs256(const std::string &coseKeyBytes, std::string *errorOut = nullptr);

// ES256 (ECDSA w/ SHA-256, P-256) verification of the ASN.1 DER signature an
// authenticator sends for alg -7, over the fixed WebAuthn signed message
//     authData || SHA-256(clientDataJSON)
// (W3C L2 §6.5.6 / §7.1 step 19 constructs exactly this). The public key is
// rebuilt from the affine coordinates via OpenSSL's EVP interface (no
// deprecated EC_KEY APIs). Any failure -> false; never throws.
bool verifyEs256Signature(const Es256PublicKey &key,
                          const std::string &authData,
                          const std::string &clientDataJson,
                          const std::string &signatureDer);

}  // namespace fulla::identity::webauthn
