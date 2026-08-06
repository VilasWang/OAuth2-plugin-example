// tests/integration/controllers/DeviceAuthEndpointHttpTest.cc
//
// HTTP integration tests for the device-authorization endpoints
// (libs/drogon/src/controllers/DeviceAuthController.cc, 162 LOC, 16% covered).
//
// /oauth2/device_authorization is the OAuth2 Device Flow (RFC 8628) start
// endpoint: a device posts its client_id (+optional scope), the server issues
// a device_code + user_code + verification_uri. It requires NO user auth (the
// device is unauthenticated at this point); only a valid client_id is needed.
// The existing tests/integration/token/DeviceCode* tests cover parts of the
// downstream token exchange; this file covers the device_authorization route
// itself + its validation branches.
//
// Route map (DeviceAuthController.h):
//   POST /oauth2/device_authorization -> deviceAuthorization (no auth)
//   POST /oauth2/device/approve       -> approveDevice (admin-gated, not here)

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include "HttpTestClient.h"

#include <string>

using authforge::test::http::loginAsAdmin;
using authforge::test::http::parseJsonBody;
using authforge::test::http::postgresAvailable;
using authforge::test::http::sendPostForm;
using authforge::test::http::serverReachable;
using authforge::test::http::statusIs;

#define DEVICEAUTH_SKIP_GUARD                                  \
    do                                                         \
    {                                                          \
        if (!postgresAvailable() || !serverReachable())        \
        {                                                      \
            CHECK(true);                                       \
            return;                                            \
        }                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// device_authorization happy path: POST with the seeded admin-console
// client_id returns 200 with the RFC 8628 device_code/user_code/
// verification_uri fields. Covers the plugin->createDeviceCode -> success
// branch. The admin-console client is PUBLIC and seeded, so no client secret
// is needed.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_DeviceAuth_AdminConsoleClient_ReturnsDeviceCode)
{
    DEVICEAUTH_SKIP_GUARD;

    auto resp = sendPostForm(
      "/oauth2/device_authorization",
      "client_id=admin-console&scope=openid profile admin");
    REQUIRE(resp != nullptr);
    // RFC 8628 §3.2: success is 200 (the device started the flow). Some servers
    // return 201; accept both. Assert the body shape regardless of which.
    const auto code = resp->getStatusCode();
    CHECK((code == drogon::k200OK || code == drogon::k201Created));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body.isMember("device_code"));
    CHECK(body["device_code"].isString());
    CHECK(body.isMember("user_code"));
    CHECK(body["user_code"].isString());
    CHECK(body.isMember("verification_uri"));
}

// ---------------------------------------------------------------------------
// device_authorization missing-client_id branch: POST with no client_id
// returns 400 invalid_request ("client_id is required"). Covers the early
// validation rejection.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_DeviceAuth_MissingClientId_Returns400)
{
    DEVICEAUTH_SKIP_GUARD;

    auto resp = sendPostForm("/oauth2/device_authorization", "scope=openid");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body.isMember("error"));
    CHECK(body["error"].asString() == "invalid_request");
}

// ---------------------------------------------------------------------------
// device_authorization unknown-client branch: POST with a client_id that does
// not exist returns an error (invalid_client or invalid_request, depending on
// the exact rejection path). Asserts a 4xx with an RFC 6749 error body.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_DeviceAuth_UnknownClientId_Returns4xxError)
{
    DEVICEAUTH_SKIP_GUARD;

    auto resp = sendPostForm(
      "/oauth2/device_authorization",
      "client_id=nonexistent-client-xyz&scope=openid");
    REQUIRE(resp != nullptr);
    const auto code = resp->getStatusCode();
    CHECK((code == drogon::k400BadRequest || code == drogon::k401Unauthorized));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body.isMember("error"));
}
