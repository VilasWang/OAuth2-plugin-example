// tests/integration/controllers/MfaEndpointHttpTest.cc
//
// HTTP integration tests for the MFA self-service endpoints
// (libs/drogon/src/controllers/MfaController.cc, 563 LOC, 30% covered).
//
// The /api/me/mfa/* routes are guarded by OAuth2AuthFilter (NOT
// AuthorizationFilter), which validates a Bearer access token and sets
// `req->attributes()["userId"]` from the token's subject (see
// OAuth2AuthFilter.cc:73). So these routes ARE HTTP-testable with a bearer
// token -- the same loginAsAdmin() flow used by the admin tests provides one.
//
// Routes (MfaController.h):
//   POST /api/me/mfa/setup    -> setup (generate secret + otpauth_uri)
//   POST /api/me/mfa/verify   -> verifySetup (confirm a TOTP code, persist)
//   POST /api/me/mfa/disable  -> disable (turn MFA off)
//   POST /oauth2/mfa/verify   -> verifyLogin (covered by the existing
//                                  MfaCrossClientAuthFix tests -- not here)
//
// Storage: Postgres-only (setup/disable hit the DB; memory mode has no admin
// user that can produce a bearer token). All cases skip cleanly under memory.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include "HttpTestClient.h"

#include <string>

using authforge::test::http::loginAsAdmin;
using authforge::test::http::parseJsonBody;
using authforge::test::http::postgresAvailable;
using authforge::test::http::sendPostJson;
using authforge::test::http::serverReachable;
using authforge::test::http::statusIs;

#define MFA_SKIP_GUARD                                          \
    do                                                          \
    {                                                           \
        if (!postgresAvailable() || !serverReachable())         \
        {                                                       \
            CHECK(true);                                        \
            return;                                             \
        }                                                       \
    } while (0)

// ---------------------------------------------------------------------------
// setup happy path: POST /api/me/mfa/setup with a valid bearer token returns
// 200 with a secret + otpauth_uri. Covers the OAuth2AuthFilter -> setupSecret
// -> success branch.
//
// Fixture note: setup marks the admin user's MFA secret as pending (stored but
// not yet verified). To keep the suite repeatable, this case follows setup
// with a disable call so the admin user is left with MFA off (matching the
// dev seed). The existing MfaCrossClientAuthFix tests use the same
// enable/restore pattern.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_Mfa_Setup_WithBearerToken_ReturnsSecret)
{
    MFA_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendPostJson("/api/me/mfa/setup", Json::Value::nullSingleton(), *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body.isMember("secret"));
    CHECK(body["secret"].isString());
    CHECK(!body["secret"].asString().empty());
    CHECK(body.isMember("otpauth_uri"));
    CHECK(body["otpauth_uri"].asString().find("otpauth://") == 0);

    // Cleanup: disable MFA so the admin user returns to its seeded (mfa off)
    // state. The disable endpoint is itself a coverage target (next case
    // asserts it independently); this call is best-effort restoration.
    sendPostJson("/api/me/mfa/disable", Json::Value::nullSingleton(), *token);
}

// ---------------------------------------------------------------------------
// setup auth guard: no bearer token -> OAuth2AuthFilter rejects with 401.
// Covers the filter's missing/invalid-token branch on a /api/me/mfa route.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_Mfa_Setup_NoToken_Returns401)
{
    MFA_SKIP_GUARD;

    auto resp = sendPostJson("/api/me/mfa/setup", Json::Value::nullSingleton());
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

// ---------------------------------------------------------------------------
// setup invalid token: a malformed bearer token -> 401 (validateAccessToken
// fails inside the filter). Covers the invalid-token branch distinct from the
// missing-token branch above.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_Mfa_Setup_InvalidToken_Returns401)
{
    MFA_SKIP_GUARD;

    auto resp = sendPostJson("/api/me/mfa/setup", Json::Value::nullSingleton(), "not-a-real-token");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

// ---------------------------------------------------------------------------
// disable happy path: with MFA enabled (via a preceding setup), POST
// /api/me/mfa/disable returns 200. Covers the disableSecret -> success branch.
// Runs setup first to ensure MFA is on, then disables; if setup fails the
// REQUIRE surfaces it. Leaves the admin user with MFA off.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_Mfa_Disable_AfterSetup_Returns200)
{
    MFA_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    // Ensure MFA is enabled first (setup persists a pending secret).
    auto setupResp = sendPostJson("/api/me/mfa/setup", Json::Value::nullSingleton(), *token);
    REQUIRE(setupResp != nullptr);
    REQUIRE(statusIs(setupResp, drogon::k200OK));

    // Disable -> 200.
    auto disResp = sendPostJson("/api/me/mfa/disable", Json::Value::nullSingleton(), *token);
    REQUIRE(disResp != nullptr);
    CHECK(statusIs(disResp, drogon::k200OK));
}

// ---------------------------------------------------------------------------
// disable auth guard: no token -> 401.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_Mfa_Disable_NoToken_Returns401)
{
    MFA_SKIP_GUARD;

    auto resp = sendPostJson("/api/me/mfa/disable", Json::Value::nullSingleton());
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}
