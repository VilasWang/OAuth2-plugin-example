// tests/integration/controllers/WebAuthnHttpTest.cc
//
// HTTP integration tests for the WebAuthn controller
// (libs/drogon/src/controllers/WebAuthnController.cc, 423 LOC, was 9.9%
// covered). Follows the MfaEndpointHttpTest pattern: the real WebAuthnService
// is wired against Postgres by bootstrap::wireIdentityServices() (the test
// binary runs the same wiring as main.cc), so these drive the real service
// over HTTP. NO mock and NO authenticator is needed: the WebAuthn
// implementation is a deliberately non-cryptographic stub
// (WebAuthnService.h:44-56) -- registerFinish trusts client-supplied
// credential_id/public_key verbatim (no signature/CBOR verification), and
// authenticateFinish just looks up the credential + bumps sign_count.
//
// Layer note: COMPLEMENT (not duplicate) WebAuthnServiceTest.cc -- that file
// tests the service result structs; these tests cover the controller layer
// (JSON body parsing, public_sub->internal id resolution via userRepo_,
// session challenge storage, audit-sink calls, full
// PublicKeyCredentialCreationOptions JSON assembly, filter integration,
// error envelopes with HTTP status codes).
//
// Storage: Postgres-only. The /api/me/webauthn/* routes are guarded by the
// DB-backed OAuth2AuthFilter (needs a bearer token from loginAsAdmin); the
// /oauth2/webauthn/authenticate/* routes' injected path also needs the
// service wired (Postgres) -- under memory mode wireIdentityServices
// early-returns, the service ptr is null, and authenticateFinish's fallback
// path calls getDbClient() which assert-crashes the process (review B2).
// So ALL cases here use the postgresAvailable() skip guard.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include "HttpTestClient.h"

#include <chrono>
#include <string>

using fulla::test::http::loginAsAdmin;
using fulla::test::http::parseJsonBody;
using fulla::test::http::postgresAvailable;
using fulla::test::http::sendPostJson;
using fulla::test::http::serverReachable;
using fulla::test::http::statusIs;

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
// Collision-resistant suffix for created credentials so repeated runs do not
// trip the unique-credential_id constraint and do not need explicit cleanup
// (rows accumulate but are harmless; Linux CI recreates the DB each run).
std::string uniqueCredId()
{
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return "cred-test-" + std::to_string(now % 1000000);
}
}  // namespace

// ===========================================================================
// Authentication flow (no auth filter on these routes, but service-wired =
// Postgres-only; review B2)
// ===========================================================================

// authenticateBegin: POST /oauth2/webauthn/authenticate/begin returns 200 with
// the PublicKeyCredentialRequestOptions-shaped JSON. Covers the
// beginAuthentication -> JSON assembly branch.
DROGON_TEST(Integration_P0_WebAuthn_AuthBegin_ReturnsChallengeShape)
{
    WEBAUTHN_SKIP_GUARD;

    auto resp =
      sendPostJson("/oauth2/webauthn/authenticate/begin", Json::Value::nullSingleton());
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    REQUIRE(body.isMember("options"));
    const auto &options = body["options"];
    CHECK(options.isMember("challenge"));
    CHECK(options["challenge"].isString());
    CHECK(!options["challenge"].asString().empty());
    CHECK(options.isMember("rpId"));
    CHECK(options.isMember("timeout"));
    CHECK(options.isMember("allowCredentials"));
}

// authenticateFinish with an unknown credential_id -> 401
// AUTH_INVALID_CREDENTIALS ("credential not found"). Covers the
// finishAuthentication miss branch.
DROGON_TEST(Integration_P1_WebAuthn_AuthFinish_UnknownCredential_Returns401)
{
    WEBAUTHN_SKIP_GUARD;

    Json::Value body;
    body["credential_id"] = "nonexistent-credential-xyz";
    auto resp = sendPostJson("/oauth2/webauthn/authenticate/finish", body);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

// authenticateFinish missing credential_id -> 400 VALIDATION_MISSING_REQUIRED_FIELD.
DROGON_TEST(Integration_P1_WebAuthn_AuthFinish_MissingCredentialId_Returns400)
{
    WEBAUTHN_SKIP_GUARD;

    auto resp =
      sendPostJson("/oauth2/webauthn/authenticate/finish", Json::Value::nullSingleton());
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}

// ===========================================================================
// Registration flow (/api/me/webauthn/*, OAuth2AuthFilter -> bearer token)
// ===========================================================================

// registerBegin: POST with a bearer token returns 200 with the full
// PublicKeyCredentialCreationOptions (challenge, rp{id,name}, user, pubKeyCredParams
// with algs -7/-257, timeout, authenticatorSelection).
DROGON_TEST(Integration_P0_WebAuthn_RegisterBegin_ReturnsCreationOptions)
{
    WEBAUTHN_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendPostJson("/api/me/webauthn/register/begin", Json::Value::nullSingleton(), *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    REQUIRE(body.isMember("options"));
    const auto &options = body["options"];
    CHECK(options.isMember("challenge"));
    CHECK(!options["challenge"].asString().empty());
    CHECK(options.isMember("rp"));
    CHECK(options["rp"].isMember("id"));
    CHECK(options.isMember("user"));
    CHECK(options.isMember("pubKeyCredParams"));
    CHECK(options.isMember("timeout"));
}

// registerBegin auth guard: no bearer token -> 401 (OAuth2AuthFilter rejects).
DROGON_TEST(Integration_P1_WebAuthn_RegisterBegin_NoToken_Returns401)
{
    WEBAUTHN_SKIP_GUARD;

    auto resp = sendPostJson("/api/me/webauthn/register/begin", Json::Value::nullSingleton());
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

// registerFinish: POST {credential_id, public_key, name} with a bearer token
// -> 201 with {message, credential_id}. Uses a unique credential_id so the
// case is repeatable without cleanup.
DROGON_TEST(Integration_P0_WebAuthn_RegisterFinish_StoresCredential_Returns201)
{
    WEBAUTHN_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    Json::Value body;
    body["credential_id"] = uniqueCredId();
    body["public_key"] = "test-public-key-blob";
    body["name"] = "test-passkey";
    auto resp = sendPostJson("/api/me/webauthn/register/finish", body, *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k201Created));
    Json::Value out;
    REQUIRE(parseJsonBody(resp, out));
    CHECK(out.isMember("credential_id"));
}

// registerFinish validation: empty credential_id -> 400
// VALIDATION_MISSING_REQUIRED_FIELD. (Empty body fields are rejected by the
// service before any DB write.)
DROGON_TEST(Integration_P1_WebAuthn_RegisterFinish_EmptyFields_Returns400)
{
    WEBAUTHN_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    Json::Value body;
    body["credential_id"] = "";
    body["public_key"] = "";
    auto resp = sendPostJson("/api/me/webauthn/register/finish", body, *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}

// registerFinish -> authenticateFinish end-to-end ceremony: INTENTIONALLY NOT
// TESTED HERE. A combined register-then-authenticate case (POST a credential
// via /api/me/webauthn/register/finish, then immediately POST it to
// /oauth2/webauthn/authenticate/finish) destabilizes the single shared
// fulla-tests process: the authenticate step's updateSignCount is an
// async DB write whose callback is still in-flight when the test binary hits
// its fast-exit (std::_Exit in test_main.cc, there to dodge Drogon's crashy
// teardown -- see test_main.cc:442-486), and the in-flight callback's
// destructor fires during process teardown, segfaulting the whole suite (and
// taking every other DROGON_TEST down with it). This was reproduced
// deterministically: the full suite passes with this case removed; it crashes
// with it present.
//
// The register-finish cases above (StoresCredential_Returns201,
// EmptyFields_Returns400) cover the register path, and the authenticate-finish
// cases above (UnknownCredential_Returns401, MissingCredentialId_Returns400)
// cover the authenticate path -- so the two halves are exercised; only the
// combined ceremony is omitted. If process-stable async-DB teardown is ever
// addressed (or a separate per-process test binary is introduced), this case
// can be re-enabled.
