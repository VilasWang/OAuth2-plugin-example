// tests/integration/controllers/UserSelfServiceEndpointHttpTest.cc
//
// HTTP integration tests for the user self-service endpoints
// (libs/drogon/src/controllers/UserSelfServiceController.cc, 406 LOC, 10%
// covered). These were flagged as "hard to test" in the initial coverage plan
// because getProfile reads `req->getAttributes()->get<std::string>("userId")`
// -- but that attribute is set by OAuth2AuthFilter (OAuth2AuthFilter.cc:73)
// from a validated Bearer access token. So all /api/me/* routes ARE
// HTTP-testable with a bearer token; loginAsAdmin() provides one.
//
// Route map (UserSelfServiceController.h):
//   GET    /api/me                       -> getProfile
//   PUT    /api/me/password              -> changePassword
//   GET    /api/me/authorized-apps       -> listAuthorizedApps
//   DELETE /api/me/authorized-apps/{cid} -> revokeAuthorizedApp
//   DELETE /api/me                       -> deleteAccount (NOT tested -- would
//                                            soft-delete the seeded admin and
//                                            break every other admin test)
//
// Storage: Postgres-only (getProfile/changePassword hit the DB; memory mode
// has no admin user that can produce a bearer token). All cases skip cleanly
// under memory.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include "HttpTestClient.h"

#include <string>

using authforge::test::http::loginAsAdmin;
using authforge::test::http::parseJsonBody;
using authforge::test::http::postgresAvailable;
using authforge::test::http::sendGet;
using authforge::test::http::sendPutJson;
using authforge::test::http::serverReachable;
using authforge::test::http::statusIs;

#define SELFSERVICE_SKIP_GUARD                                 \
    do                                                         \
    {                                                          \
        if (!postgresAvailable() || !serverReachable())        \
        {                                                      \
            CHECK(true);                                       \
            return;                                            \
        }                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// getProfile happy path: GET /api/me with a bearer token returns 200 with the
// admin user's profile fields. Covers the OAuth2AuthFilter -> findOne by
// public_sub -> success branch.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_UserSelfService_GetProfile_ReturnsAdminProfile)
{
    SELFSERVICE_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendGet("/api/me", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["username"].asString() == "admin");
    CHECK(body.isMember("email"));
    CHECK(body.isMember("email_verified"));
    CHECK(body.isMember("mfa_enabled"));
}

// ---------------------------------------------------------------------------
// getProfile auth guard: no token -> OAuth2AuthFilter rejects with 401.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_UserSelfService_GetProfile_NoToken_Returns401)
{
    SELFSERVICE_SKIP_GUARD;

    auto resp = sendGet("/api/me");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

// ---------------------------------------------------------------------------
// getProfile invalid token: malformed bearer -> 401 (validateAccessToken
// fails). Covers the invalid-token branch.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_UserSelfService_GetProfile_InvalidToken_Returns401)
{
    SELFSERVICE_SKIP_GUARD;

    auto resp = sendGet("/api/me", "not-a-real-token");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

// ---------------------------------------------------------------------------
// listAuthorizedApps happy path: GET /api/me/authorized-apps with a bearer
// token returns 200 with an array (possibly empty for the admin user). Covers
// the authorized-apps listing branch.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_UserSelfService_ListAuthorizedApps_Returns200)
{
    SELFSERVICE_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendGet("/api/me/authorized-apps", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    // Body shape varies (array or {authorized_apps:[...]}); just require 200 +
    // a parseable body, which the REQUIRE(parseJsonBody) above already ensures
    // when we parse it.
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
}

// ---------------------------------------------------------------------------
// changePassword validation branch: PUT /api/me/password with an empty body
// returns 400 (missing current_password / new_password). Covers the
// early-validation rejection without mutating the admin password.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_UserSelfService_ChangePassword_EmptyBody_Returns4xx)
{
    SELFSERVICE_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendPutJson("/api/me/password", Json::Value::nullSingleton(), *token);
    REQUIRE(resp != nullptr);
    // Empty body -> validation error (400) -- the exact shape depends on the
    // handler's validation; assert a 4xx (not 200, which would mean it ignored
    // the empty body; not 500).
    const auto code = resp->getStatusCode();
    CHECK((code == drogon::k400BadRequest || code == drogon::k401Unauthorized));
}
