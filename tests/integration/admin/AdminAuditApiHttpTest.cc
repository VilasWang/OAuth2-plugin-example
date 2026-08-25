// tests/integration/admin/AdminAuditApiHttpTest.cc
//
// HTTP integration tests for the audit admin API
// (libs/drogon/src/admin/AuditService.cc + controllers/AuditController.cc).
//
// Coverage target: AuditService (294 LOC, 0% today) + AuditController (80 LOC).
//
// Storage: Postgres-only. listLogs and getDashboardStats call getDbClient()
// directly; dashboard is metadata-only (no DB) but still lives under
// /api/admin/* (requires the admin token). All cases skip cleanly under memory.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include "HttpTestClient.h"

#include <string>

using fulla::test::http::loginAsAdmin;
using fulla::test::http::parseJsonBody;
using fulla::test::http::postgresAvailable;
using fulla::test::http::sendGet;
using fulla::test::http::serverReachable;
using fulla::test::http::statusIs;

#define ADMIN_AUDIT_SKIP_GUARD                                \
    do                                                        \
    {                                                         \
        if (!postgresAvailable() || !serverReachable())       \
        {                                                     \
            CHECK(true);                                      \
            return;                                           \
        }                                                     \
    } while (0)

// ---------------------------------------------------------------------------
// dashboard (metadata-only) happy path: GET /api/admin/dashboard -> 200 with
// a static welcome body. This endpoint does not touch the DB; it still
// requires the admin token, so it runs only under Postgres.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_AdminAudit_Dashboard_Returns200)
{
    ADMIN_AUDIT_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendGet("/api/admin/dashboard", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
}

// ---------------------------------------------------------------------------
// dashboard auth guard: no token -> 401.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminAudit_Dashboard_NoToken_Returns401)
{
    ADMIN_AUDIT_SKIP_GUARD;

    auto resp = sendGet("/api/admin/dashboard");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

// ---------------------------------------------------------------------------
// listLogs happy path: GET /api/admin/logs -> 200 with the paginated logs
// shape {status, page, per_page, total, logs}. The preceding admin login
// writes a login-success audit row, so at least one log exists.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_AdminAudit_ListLogs_Returns200)
{
    ADMIN_AUDIT_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendGet("/api/admin/logs", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["status"].asString() == "success");
    CHECK(body.isMember("logs"));
    CHECK(body.isMember("page"));
    CHECK(body.isMember("per_page"));
    CHECK(body.isMember("total"));
}

// ---------------------------------------------------------------------------
// listLogs auth guard: no token -> 401.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminAudit_ListLogs_NoToken_Returns401)
{
    ADMIN_AUDIT_SKIP_GUARD;

    auto resp = sendGet("/api/admin/logs");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

// ---------------------------------------------------------------------------
// getDashboardStats happy path: GET /api/admin/dashboard/stats -> 200 with
// the aggregate stats shape {status, total_users, total_clients,
// active_tokens, logs_today, failures_today}. Exercises the multi-table count
// aggregation branch in AuditService.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_AdminAudit_DashboardStats_Returns200)
{
    ADMIN_AUDIT_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendGet("/api/admin/dashboard/stats", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["status"].asString() == "success");
    CHECK(body.isMember("total_users"));
    CHECK(body.isMember("total_clients"));
    CHECK(body.isMember("active_tokens"));
    CHECK(body.isMember("logs_today"));
    CHECK(body.isMember("failures_today"));
}

// ---------------------------------------------------------------------------
// listLogs pagination param: GET /api/admin/logs?page=1&per_page=5 -> 200 and
// echoes the requested page/per_page. Covers the query-param parsing branch.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminAudit_ListLogs_PaginationParams_Honored)
{
    ADMIN_AUDIT_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendGet("/api/admin/logs?page=2&per_page=3", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["page"].asInt() == 2);
    CHECK(body["per_page"].asInt() == 3);
}
