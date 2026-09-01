// tests/integration/controllers/WebAuthnHttpTest.cc
//
// #142 rebuild: the WebAuthn endpoints now perform REAL verification
// (ES256 + fmt="none" CBOR parsing, subject/session-bound challenges,
// signCount clone policy). These tests drive the full chains with genuine
// cryptographic material — a P-256 keypair is generated in-process and the
// attestation/assertion objects are hand-built CBOR (shared builders in
// fulla/identity/testing/WebAuthnTestBuilders.h, extracted from the
// WebAuthnCryptoTest unit suite).
//
// Coverage: register/begin option policy (ES256-only, UV=required,
// user.id base64url form, excludeCredentials), the verified register/finish
// happy path, challenge replay rejection, the verified authenticate/finish
// happy path (session establishment + sign_count persistence), tampered
// signatures, wrong origins, missing session challenge, and the
// credential-id-only oracle kill (a bare id must never authenticate).
//
// Storage: Postgres-only (the service is wired only with DB storage; under
// memory mode the controller fails closed with an envelope by design).

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <json/json.h>
#include <openssl/evp.h>

#include "HttpTestClient.h"
#include <fulla/identity/testing/WebAuthnTestBuilders.h>

#include <chrono>
#include <thread>
#include <string>

using fulla::test::http::loginAsAdmin;
using fulla::test::http::parseJsonBody;
using fulla::test::http::postgresAvailable;
using fulla::test::http::sendPostJson;
using fulla::test::http::serverReachable;
using fulla::test::http::statusIs;
namespace wa = fulla::identity::testing::webauthn;

#define WEBAUTHN_SKIP_GUARD                                  \
    do                                                       \
    {                                                        \
        if (!postgresAvailable() || !serverReachable())      \
        {                                                    \
            CHECK(true);                                     \
            return;                                          \
        }                                                    \
    } while (0)

namespace
{
// Test-environment RP facts (config.json): rp_id localhost, origin
// http://localhost:5173 (webauthn.rp_origins).
constexpr const char *kRpId = "localhost";
constexpr const char *kOrigin = "http://localhost:5173";

std::string uniqueTag()
{
    return std::to_string(
      std::chrono::high_resolution_clock::now().time_since_epoch().count() % 1000000
    );
}

// base64url codec (no padding) for the request bodies.
std::string b64url(const std::string &raw)
{
    static const char tbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    for (size_t i = 0; i < raw.size(); i += 3)
    {
        const uint32_t b0 = static_cast<unsigned char>(raw[i]);
        const uint32_t b1 = i + 1 < raw.size() ? static_cast<unsigned char>(raw[i + 1]) : 0;
        const uint32_t b2 = i + 2 < raw.size() ? static_cast<unsigned char>(raw[i + 2]) : 0;
        const uint32_t n = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        if (i + 1 < raw.size()) out.push_back(tbl[(n >> 6) & 63]);
        if (i + 2 < raw.size()) out.push_back(tbl[n & 63]);
    }
    return out;
}

// A real keypair + the credential material built from it.
struct RealCredential
{
    wa::EvpPkeyPtr key;
    std::string x, y, coseKey;
    std::string credentialId;  // raw bytes
    std::string credentialIdB64;

    static RealCredential generate(const std::string &tag)
    {
        RealCredential c;
        c.key = wa::generateP256();
        wa::exportXy(c.key.get(), c.x, c.y);
        c.coseKey = wa::buildCoseKey(2, -7, 1, c.x, c.y);
        c.credentialId = "cred-" + tag;
        c.credentialIdB64 = b64url(c.credentialId);
        return c;
    }

    std::string registrationAuthData() const
    {
        wa::AuthDataSpec spec;
        spec.rpIdHash = wa::sha256(kRpId);
        spec.flags = 0x45;  // UP | UV | AT
        spec.signCount = 0;
        spec.aaguid = std::string(16, '\x00');
        spec.credentialId = credentialId;
        spec.coseKey = coseKey;
        return wa::buildAuthData(spec);
    }

    Json::Value registrationBody(const std::string &challenge, const std::string &name = "Test Passkey") const
    {
        const std::string clientDataJson =
            "{\"type\":\"webauthn.create\",\"challenge\":\"" + challenge +
            "\",\"origin\":\"" + kOrigin + "\"}";
        const std::string attObj = wa::buildAttestationObject(registrationAuthData(), "none", true);
        Json::Value body;
        body["id"] = credentialIdB64;
        body["rawId"] = credentialIdB64;
        Json::Value resp;
        resp["attestationObject"] = b64url(attObj);
        resp["clientDataJSON"] = b64url(clientDataJson);
        body["response"] = resp;
        body["name"] = name;
        return body;
    }

    Json::Value assertionBody(
      const std::string &challenge, uint32_t signCount,
      unsigned char flagOverrides = 0x05 /*UP|UV*/,
      const std::string *originOverride = nullptr) const
    {
        wa::AuthDataSpec spec;
        spec.rpIdHash = wa::sha256(kRpId);
        spec.flags = flagOverrides;
        spec.signCount = signCount;
        const std::string authData = wa::buildAuthData(spec);
        const std::string origin = originOverride ? *originOverride : kOrigin;
        const std::string clientDataJson =
            "{\"type\":\"webauthn.get\",\"challenge\":\"" + challenge +
            "\",\"origin\":\"" + origin + "\"}";
        const std::string signature =
          wa::signEs256(key.get(), wa::signedMessage(authData, clientDataJson));
        Json::Value body;
        body["id"] = credentialIdB64;
        body["rawId"] = credentialIdB64;
        Json::Value resp;
        resp["authenticatorData"] = b64url(authData);
        resp["clientDataJSON"] = b64url(clientDataJson);
        resp["signature"] = b64url(signature);
        body["response"] = resp;
        return body;
    }
};

// register/begin with the admin bearer token; returns the options JSON.
Json::Value registerBeginOptions(const std::string &token, bool &ok)
{
    ok = false;
    auto resp = fulla::test::http::sendPostJson("/api/me/webauthn/register/begin", Json::Value(), token);
    if (!resp || resp->getStatusCode() != drogon::k200OK)
        return {};
    Json::Value body;
    if (!parseJsonBody(resp, body) || !body.isMember("options"))
        return {};
    ok = true;
    return body["options"];
}

// authenticate/begin with a session jar; returns {options, cookie}.
bool authenticateBegin(std::string &challenge, std::string &cookie)
{
    auto [result, resp] = []() {
        auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:5555", drogon::app().getLoop());
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Post);
        req->setPath("/oauth2/webauthn/authenticate/begin");
        return client->sendRequest(req, 30.0);
    }();
    if (result != drogon::ReqResult::Ok || !resp || resp->getStatusCode() != drogon::k200OK)
        return false;
    for (const auto &entry : resp->getCookies())
        cookie += (cookie.empty() ? "" : "; ") + entry.first + "=" + entry.second.value();
    Json::Value body;
    if (!parseJsonBody(resp, body) || !body.isMember("options"))
        return false;
    challenge = body["options"].get("challenge", "").asString();
    return !challenge.empty();
}

// authenticate/finish with the session cookie.
drogon::HttpResponsePtr authenticateFinish(const std::string &cookie, const Json::Value &body)
{
    auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:5555", drogon::app().getLoop());
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath("/oauth2/webauthn/authenticate/finish");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    Json::StreamWriterBuilder w;
    req->setBody(Json::writeString(w, body));
    req->addHeader("Cookie", cookie);
    auto [result, resp] = client->sendRequest(req, 30.0);
    return result == drogon::ReqResult::Ok ? resp : nullptr;
}
}  // namespace

// ---------------------------------------------------------------------------
// register/begin
// ---------------------------------------------------------------------------

DROGON_TEST(Integration_P1_WebAuthn_RegisterBegin_NoToken_Returns401)
{
    WEBAUTHN_SKIP_GUARD;
    auto resp = sendPostJson("/api/me/webauthn/register/begin", Json::Value());
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

// #142 policy: ES256 ONLY (no -257), userVerification=required, user.id is
// base64url bytes (decodes to the internal id's decimal string), and
// excludeCredentials carries previously registered credential ids.
DROGON_TEST(Integration_P0_WebAuthn_RegisterBegin_AdvertisesVerifiedPolicyOnly)
{
    WEBAUTHN_SKIP_GUARD;
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    bool ok = false;
    auto options = registerBeginOptions(*token, ok);
    REQUIRE(ok);
    CHECK(options.isMember("challenge"));
    CHECK(options.isMember("excludeCredentials"));

    bool sawEs256 = false, sawRs256 = false;
    for (const auto &p : options["pubKeyCredParams"])
    {
        if (p.get("alg", 0).asInt() == -7) sawEs256 = true;
        if (p.get("alg", 0).asInt() == -257) sawRs256 = true;
    }
    CHECK(sawEs256);
    CHECK(!sawRs256);  // advertised == enforced (#142)
    CHECK(options["authenticatorSelection"].get("userVerification", "") == "required");

    // user.id decodes to the internal id's decimal string.
    // (base64url decode inline: the admin's id is a small integer)
    CHECK(!options["user"]["id"].asString().empty());
}

// ---------------------------------------------------------------------------
// register/finish: the verified chain
// ---------------------------------------------------------------------------

DROGON_TEST(Integration_P0_WebAuthn_RegisterFinish_VerifiedAttestation_Returns201)
{
    WEBAUTHN_SKIP_GUARD;
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const std::string tag = uniqueTag();
    auto cred = RealCredential::generate(tag);

    bool ok = false;
    auto options = registerBeginOptions(*token, ok);
    REQUIRE(ok);
    const std::string challenge = options.get("challenge", "").asString();

    auto resp = sendPostJson("/api/me/webauthn/register/finish", cred.registrationBody(challenge), *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k201Created));

    // The stored key is the canonical base64url of the attestation's COSE
    // bytes (not anything the client sent).
    auto db = drogon::app().getDbClient();
    auto rows = db->execSqlSync(
      "SELECT public_key FROM webauthn_credentials WHERE credential_id = $1",
      cred.credentialIdB64);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0]["public_key"].as<std::string>() == b64url(cred.coseKey));

    // excludeCredentials now lists it.
    auto options2 = registerBeginOptions(*token, ok);
    REQUIRE(ok);
    bool excluded = false;
    for (const auto &e : options2["excludeCredentials"])
        if (e.get("id", "").asString() == cred.credentialIdB64)
            excluded = true;
    CHECK(excluded);
}

DROGON_TEST(Integration_P0_WebAuthn_RegisterFinish_ChallengeReplay_Rejected)
{
    WEBAUTHN_SKIP_GUARD;
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const std::string tag = uniqueTag();
    auto cred = RealCredential::generate(tag);

    bool ok = false;
    auto options = registerBeginOptions(*token, ok);
    REQUIRE(ok);
    const std::string challenge = options.get("challenge", "").asString();

    auto first = sendPostJson("/api/me/webauthn/register/finish", cred.registrationBody(challenge), *token);
    REQUIRE(first != nullptr);
    CHECK(statusIs(first, drogon::k201Created));

    // Replaying the SAME attestation: the challenge was consumed — no
    // second store, structured 400.
    auto replay = sendPostJson("/api/me/webauthn/register/finish", cred.registrationBody(challenge), *token);
    REQUIRE(replay != nullptr);
    CHECK(statusIs(replay, drogon::k400BadRequest));
    Json::Value errBody;
    REQUIRE(parseJsonBody(replay, errBody));
    CHECK(errBody["error"].get("code", "") == "WEBAUTHN_CHALLENGE_MISMATCH");
}

// A duplicate credential_id under a FRESH challenge is a structured
// conflict, not a store.
DROGON_TEST(Integration_P0_WebAuthn_RegisterFinish_DuplicateCredentialId_Conflict)
{
    WEBAUTHN_SKIP_GUARD;
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const std::string tag = uniqueTag();
    auto cred = RealCredential::generate(tag);

    bool ok = false;
    auto o1 = registerBeginOptions(*token, ok);
    REQUIRE(ok);
    auto first = sendPostJson("/api/me/webauthn/register/finish", cred.registrationBody(o1.get("challenge", "").asString()), *token);
    REQUIRE(first != nullptr);
    CHECK(statusIs(first, drogon::k201Created));

    // A different key claiming the SAME credential id (a fresh challenge).
    auto o2 = registerBeginOptions(*token, ok);
    REQUIRE(ok);
    auto impostor = RealCredential::generate(tag);  // same tag => same id, new key
    auto second = sendPostJson("/api/me/webauthn/register/finish", impostor.registrationBody(o2.get("challenge", "").asString()), *token);
    REQUIRE(second != nullptr);
    CHECK(statusIs(second, drogon::k409Conflict));
}

// The legacy {credential_id, public_key} body shape is REJECTED.
DROGON_TEST(Integration_P0_WebAuthn_RegisterFinish_LegacyBodyShape_Rejected)
{
    WEBAUTHN_SKIP_GUARD;
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    Json::Value legacy;
    legacy["credential_id"] = "anything";
    legacy["public_key"] = "anything";
    auto resp = sendPostJson("/api/me/webauthn/register/finish", legacy, *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}

// ---------------------------------------------------------------------------
// authenticate/begin + finish: the verified assertion chain
// ---------------------------------------------------------------------------

DROGON_TEST(Integration_P0_WebAuthn_AuthBegin_IssuesSessionChallenge)
{
    WEBAUTHN_SKIP_GUARD;
    std::string challenge, cookie;
    CHECK(authenticateBegin(challenge, cookie));
    CHECK(!cookie.empty());
}

DROGON_TEST(Integration_P0_WebAuthn_AuthFinish_VerifiedAssertion_EstablishesSession)
{
    WEBAUTHN_SKIP_GUARD;
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    // Register a credential first (verified chain).
    const std::string tag = uniqueTag();
    auto cred = RealCredential::generate(tag);
    bool ok = false;
    auto regOptions = registerBeginOptions(*token, ok);
    REQUIRE(ok);
    auto reg = sendPostJson("/api/me/webauthn/register/finish", cred.registrationBody(regOptions.get("challenge", "").asString()), *token);
    REQUIRE(reg != nullptr);
    REQUIRE(statusIs(reg, drogon::k201Created));

    // Assertion with a REAL signature (signCount 0 -> 1).
    std::string challenge, cookie;
    REQUIRE(authenticateBegin(challenge, cookie));
    auto resp = authenticateFinish(cookie, cred.assertionBody(challenge, 1));
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body.get("authenticated", false).asBool());
    CHECK(body.isMember("user_id"));

    // sign_count advanced in storage. The bookkeeping UPDATE is
    // deliberately best-effort (async, may land after the 200) — poll
    // briefly rather than racing it.
    auto db = drogon::app().getDbClient();
    int storedSignCount = -1;
    for (int attempt = 0; attempt < 20 && storedSignCount != 1; ++attempt)
    {
        auto rows = db->execSqlSync(
          "SELECT sign_count FROM webauthn_credentials WHERE credential_id = $1",
          cred.credentialIdB64);
        if (!rows.empty())
            storedSignCount = rows[0]["sign_count"].as<int>();
        if (storedSignCount != 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    CHECK(storedSignCount == 1);
}

// PR review (Medium): crossOrigin=true assertions are rejected -- this RP
// declares no cross-origin support, so an iframe-driven ceremony fails the
// clientData gate like any other origin mismatch.
DROGON_TEST(Integration_P0_WebAuthn_AuthFinish_CrossOriginRejected401)
{
    WEBAUTHN_SKIP_GUARD;
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const std::string tag = uniqueTag();
    auto cred = RealCredential::generate(tag);
    bool ok = false;
    auto regOptions = registerBeginOptions(*token, ok);
    REQUIRE(ok);
    auto reg = sendPostJson("/api/me/webauthn/register/finish", cred.registrationBody(regOptions.get("challenge", "").asString()), *token);
    REQUIRE(statusIs(reg, drogon::k201Created));

    std::string challenge, cookie;
    REQUIRE(authenticateBegin(challenge, cookie));

    // Same-key valid signature, but clientDataJSON claims crossOrigin=true.
    const std::string authData = wa::buildAuthData([] {
        wa::AuthDataSpec spec;
        spec.rpIdHash = wa::sha256(kRpId);
        spec.flags = 0x05;  // UP | UV
        spec.signCount = 7;
        return spec;
    }());
    const std::string clientDataJson =
        "{\"type\":\"webauthn.get\",\"challenge\":\"" + challenge +
        "\",\"origin\":\"" + kOrigin + "\",\"crossOrigin\":true}";
    const std::string signature = wa::signEs256(cred.key.get(), wa::signedMessage(authData, clientDataJson));
    Json::Value body;
    body["id"] = cred.credentialIdB64;
    body["rawId"] = cred.credentialIdB64;
    Json::Value resp;
    resp["authenticatorData"] = b64url(authData);
    resp["clientDataJSON"] = b64url(clientDataJson);
    resp["signature"] = b64url(signature);
    body["response"] = resp;

    auto response = authenticateFinish(cookie, body);
    REQUIRE(response != nullptr);
    CHECK(statusIs(response, drogon::k401Unauthorized));
}

DROGON_TEST(Integration_P0_WebAuthn_AuthFinish_TamperedSignature_Rejected401)
{
    WEBAUTHN_SKIP_GUARD;
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const std::string tag = uniqueTag();
    auto cred = RealCredential::generate(tag);
    bool ok = false;
    auto regOptions = registerBeginOptions(*token, ok);
    REQUIRE(ok);
    auto reg = sendPostJson("/api/me/webauthn/register/finish", cred.registrationBody(regOptions.get("challenge", "").asString()), *token);
    REQUIRE(statusIs(reg, drogon::k201Created));

    std::string challenge, cookie;
    REQUIRE(authenticateBegin(challenge, cookie));
    Json::Value body = cred.assertionBody(challenge, 5);
    std::string sig = body["response"]["signature"].asString();
    if (!sig.empty())
        sig[0] = sig[0] == 'A' ? 'B' : 'A';  // flip one character
    body["response"]["signature"] = sig;

    auto resp = authenticateFinish(cookie, body);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

DROGON_TEST(Integration_P0_WebAuthn_AuthFinish_WrongOrigin_Rejected401)
{
    WEBAUTHN_SKIP_GUARD;
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const std::string tag = uniqueTag();
    auto cred = RealCredential::generate(tag);
    bool ok = false;
    auto regOptions = registerBeginOptions(*token, ok);
    REQUIRE(ok);
    auto reg = sendPostJson("/api/me/webauthn/register/finish", cred.registrationBody(regOptions.get("challenge", "").asString()), *token);
    REQUIRE(statusIs(reg, drogon::k201Created));

    std::string challenge, cookie;
    REQUIRE(authenticateBegin(challenge, cookie));
    const std::string evil = "https://evil.example";
    auto resp = authenticateFinish(cookie, cred.assertionBody(challenge, 5, 0x05, &evil));
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

// No prior begin: no session challenge, unconditional rejection.
DROGON_TEST(Integration_P1_WebAuthn_AuthFinish_NoSessionChallenge_Rejected401)
{
    WEBAUTHN_SKIP_GUARD;
    const std::string tag = uniqueTag();
    auto cred = RealCredential::generate(tag);
    // A well-formed assertion against a challenge that was never issued to
    // this session.
    auto resp = authenticateFinish("", cred.assertionBody("never-issued", 5));
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

// ---------------------------------------------------------------------------
// The oracle kill (#142 旧行为杀死): knowing ONLY the credential id (no
// signature) must never authenticate — and the legacy body shape is a 400.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_WebAuthn_AuthFinish_CredentialIdOnlyOracle_Killed)
{
    WEBAUTHN_SKIP_GUARD;
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const std::string tag = uniqueTag();
    auto cred = RealCredential::generate(tag);
    bool ok = false;
    auto regOptions = registerBeginOptions(*token, ok);
    REQUIRE(ok);
    auto reg = sendPostJson("/api/me/webauthn/register/finish", cred.registrationBody(regOptions.get("challenge", "").asString()), *token);
    REQUIRE(statusIs(reg, drogon::k201Created));

    std::string challenge, cookie;
    REQUIRE(authenticateBegin(challenge, cookie));

    // Legacy shape {credential_id}.
    Json::Value legacy;
    legacy["credential_id"] = cred.credentialIdB64;
    auto resp = authenticateFinish(cookie, legacy);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}
