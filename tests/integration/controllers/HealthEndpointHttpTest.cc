// tests/integration/controllers/HealthEndpointHttpTest.cc
//
// HTTP integration tests for the health endpoints
// (libs/drogon/src/controllers/HealthController.cc, 150 LOC, partially
// covered). These are MEMORY-SAFE -- they run in every CI leg (Windows
// memory-mode included) because the health routes do not require auth and
// the liveness/basic-health paths do not touch the DB.
//
// Route map (HealthController.h):
//   GET /health        -> health()      (plugin storage_type in body; no DB call)
//   GET /health/live   -> healthLive()  (always 200, pure metadata)
//   GET /health/ready  -> healthReady() (DB + Redis probe; under memory mode
//                          getDbClient() is not configured, so this returns 503
//                          -- the catch(...) branch at HealthController.cc:139)

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include "HttpTestClient.h"

#include <string>

using authforge::test::http::parseJsonBody;
using authforge::test::http::postgresAvailable;
using authforge::test::http::sendGet;
using authforge::test::http::serverReachable;
using authforge::test::http::statusIs;

// /health/live is the cheapest reachability probe and the only health route
// that is guaranteed 200 with no side effects. serverReachable() in the
// shared header already uses it.
DROGON_TEST(Integration_P0_Health_Live_Returns200OkStatus)
{
    // No skip guard: /health/live is always available once the app is up, in
    // every storage mode. The serverReachable() probe inside sendGet's client
    // construction is implicit; if the server truly isn't up, sendGet returns
    // nullptr and the REQUIRE surfaces it.
    auto resp = sendGet("/health/live");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["status"].asString() == "ok");
}

// /health (basic) returns 200 with the plugin's storage_type in the body.
// Memory-safe: it only reads plugin->getStorageType(), no DB call.
DROGON_TEST(Integration_P0_Health_Basic_Returns200WithStorageType)
{
    auto resp = sendGet("/health");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["status"].asString() == "ok");
    CHECK(body.isMember("storage_type"));
    CHECK(body["database"].asString() == "connected");
}

// /health/ready branches on DB availability:
//  - Postgres configured + up     -> 200 {status:ok, database:connected, redis:connected}
//  - Postgres configured + down   -> 503 {status:unhealthy, database:disconnected}
//  - memory mode (no db_clients)  -> 503 (the getDbClient() catch(...) branch)
// Assert the response is one of these shapes; the exact status depends on the
// run environment, so check the body shape rather than a hard status.
DROGON_TEST(Integration_P1_Health_Ready_ReturnsPlausibleStatus)
{
    // SKIP under memory mode: healthReady() calls drogon::app().getDbClient()
    // (HealthController.cc:81), and in memory mode db_clients is empty -- the
    // framework hits an uncatchable assert (process-terminating, not a throw;
    // see tests/contract/ContractFixtures.h:77-91 for the documented trap).
    // The /health/ready route is genuinely only meaningful when a DB is
    // configured, so skipping it under memory is correct behavior.
    if (!postgresAvailable())
    {
        CHECK(true);
        return;
    }

    auto resp = sendGet("/health/ready");
    REQUIRE(resp != nullptr);
    // Status is either 200 (DB+Redis up) or 503 (DB down). Under Postgres with
    // Redis also up, expect 200.
    const auto code = resp->getStatusCode();
    CHECK((code == drogon::k200OK || code == drogon::k503ServiceUnavailable));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body.isMember("status"));
    CHECK(body.isMember("database"));
}
