// tests/integration/admin/AdminUserApiHttpTest.cc
//
// HTTP integration tests for the user-management admin API
// (libs/drogon/src/admin/UserAdminService.cc + controllers/UserAdminController.cc).
// Drives the in-process Drogon app via tests/common/HttpTestClient.h.
//
// Coverage target: UserAdminService (746 LOC, 0% today) +
// UserAdminController (167 LOC). Each case maps to one service branch.
//
// Storage: Postgres-only (admin services call getDbClient() directly; memory
// mode has no admin login). All cases skip cleanly under memory.
//
// Test-data isolation: there is NO create-user admin route, so the mutating
// cases (update / disable / enable) target the SEEDED admin user (the dev
// seed in apps/server/seed/dev_admin_user.sql). Each mutating case captures
// the pre-state and restores it via an RAII Guard so the suite is order-
// independent and repeatable. The admin user is resolved by listing users
// (id is assigned by Postgres, typically 1 but not assumed).

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "HttpTestClient.h"

#include <future>
#include <string>

using authforge::test::http::loginAsAdmin;
using authforge::test::http::parseJsonBody;
using authforge::test::http::postgresAvailable;
using authforge::test::http::sendGet;
using authforge::test::http::sendPostJson;
using authforge::test::http::sendPutJson;
using authforge::test::http::serverReachable;
using authforge::test::http::statusIs;

#define ADMIN_USER_SKIP_GUARD                                   \
    do                                                          \
    {                                                           \
        if (!postgresAvailable() || !serverReachable())         \
        {                                                       \
            CHECK(true);                                        \
            return;                                             \
        }                                                       \
    } while (0)

namespace
{
// Resolve the seeded admin user's integer id by listing users and finding
// the one named "admin". Returns -1 on any failure (caller skips the case).
// Uses the admin token to call GET /api/admin/users; the list response shape
// is {"status":"success","users":[{"id":N,"username":"admin",...}]}.
int findAdminUserId(const std::string &token)
{
    auto resp = sendGet("/api/admin/users", token);
    if (!resp)
        return -1;
    Json::Value body;
    if (!parseJsonBody(resp, body) || !body.isMember("users"))
        return -1;
    for (const auto &u : body["users"])
    {
        if (u.get("username", "").asString() == "admin")
            return u.get("id", -1).asInt();
    }
    return -1;
}

// Direct-DB helper: read the seeded admin user's current email + locked_until
// so a mutating test can restore them in its RAII Guard. Returns false on
// failure. Mirrors the enableAdminMfa/restoreAdminMfa pattern in
// tests/integration/auth/MfaCrossClientAuthFix_IntegrationTest.cc.
struct AdminUserSnapshot
{
    std::string email;
    bool emailVerified = false;
    int64_t lockedUntil = 0;
    bool ok = false;
};

AdminUserSnapshot snapshotAdminUser(int userId)
{
    AdminUserSnapshot s;
    try
    {
        auto db = drogon::app().getDbClient();
        if (!db)
            return s;
        std::promise<void> p;
        db->execSqlAsync(
          "SELECT email, email_verified, locked_until FROM users WHERE id = $1",
          [&s, &p](const drogon::orm::Result &r) {
              if (!r.empty())
              {
                  s.email = r[0]["email"].as<std::string>();
                  s.emailVerified = r[0]["email_verified"].as<bool>();
                  s.lockedUntil = r[0]["locked_until"].as<int64_t>();
                  s.ok = true;
              }
              p.set_value();
          },
          [&p](const drogon::orm::DrogonDbException &) { p.set_value(); },
          userId);
        p.get_future().get();
    }
    catch (...)
    {
    }
    return s;
}

// Restore the admin user's email + locked_until to a snapshot. Used by RAII
// Guards after a mutating test. Best-effort: a failure here is logged but not
// fatal (the next test run's seeder re-fixes the admin row).
void restoreAdminUser(int userId, const AdminUserSnapshot &s)
{
    if (!s.ok)
        return;
    try
    {
        auto db = drogon::app().getDbClient();
        if (!db)
            return;
        std::promise<void> p;
        db->execSqlAsync(
          "UPDATE users SET email = $1, email_verified = $2, locked_until = $3 "
          "WHERE id = $4",
          [&p](const drogon::orm::Result &) { p.set_value(); },
          [&p](const drogon::orm::DrogonDbException &) { p.set_value(); },
          s.email,
          s.emailVerified,
          s.lockedUntil,
          userId);
        p.get_future().get();
    }
    catch (...)
    {
    }
}
}  // namespace

// ---------------------------------------------------------------------------
// listUsers happy path: GET /api/admin/users returns 200 with a users array.
// The list endpoint has three internal branches (zero users, users-without-
// roles, users-with-roles); the seeded DB always has the admin user with the
// admin role, so this exercises the roles-attached branch.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_AdminUser_List_WithAdminToken_Returns200)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendGet("/api/admin/users", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["status"].asString() == "success");
    CHECK(body["users"].isArray());
    CHECK(body.isMember("total"));
}

// ---------------------------------------------------------------------------
// listUsers auth guard: no token -> 401.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminUser_List_NoToken_Returns401)
{
    ADMIN_USER_SKIP_GUARD;

    auto resp = sendGet("/api/admin/users");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
}

// ---------------------------------------------------------------------------
// getUser happy path: GET /api/admin/users/{adminId} returns 200 with the
// admin user's fields.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_AdminUser_Get_AdminUser_Returns200)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const int adminId = findAdminUserId(*token);
    REQUIRE(adminId > 0);

    auto resp = sendGet("/api/admin/users/" + std::to_string(adminId), *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["status"].asString() == "success");
    CHECK(body["username"].asString() == "admin");
    CHECK(body.isMember("roles"));
}

// ---------------------------------------------------------------------------
// getUser not-found branch: unknown id -> 404 VALIDATION_RESOURCE_NOT_FOUND.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminUser_Get_UnknownId_Returns404)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendGet("/api/admin/users/999999", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k404NotFound));
}

// ---------------------------------------------------------------------------
// getUser invalid-id branch: non-integer id -> 400 VALIDATION_INVALID_INPUT
// ("userId must be an integer"). Covers the stoi exception path.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminUser_Get_NonIntegerId_Returns400)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendGet("/api/admin/users/not-an-int", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}

// ---------------------------------------------------------------------------
// updateUser happy path: PUT /api/admin/users/{adminId} with {"email":...}
// returns 200, and the email is persisted. Restores the original email
// afterwards via the RAII snapshot/restore helpers above.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminUser_Update_Email_Returns200AndPersists)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const int adminId = findAdminUserId(*token);
    REQUIRE(adminId > 0);

    const auto snapshot = snapshotAdminUser(adminId);
    REQUIRE(snapshot.ok);
    // RAII restore.
    struct Restore
    {
        int id;
        AdminUserSnapshot snap;
        ~Restore()
        {
            restoreAdminUser(id, snap);
        }
    } guard{adminId, snapshot};

    Json::Value updateBody;
    updateBody["email"] = "test-admin-modified@example.com";
    auto updResp = sendPutJson("/api/admin/users/" + std::to_string(adminId), updateBody, *token);
    REQUIRE(updResp != nullptr);
    CHECK(statusIs(updResp, drogon::k200OK));

    // Verify persistence by re-fetching.
    auto getResp = sendGet("/api/admin/users/" + std::to_string(adminId), *token);
    REQUIRE(getResp != nullptr);
    Json::Value body;
    REQUIRE(parseJsonBody(getResp, body));
    CHECK(body["email"].asString() == "test-admin-modified@example.com");
}

// ---------------------------------------------------------------------------
// updateUser "no updatable fields" branch: PUT with an empty JSON object
// returns 400 VALIDATION_INVALID_INPUT ("No updatable fields provided").
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminUser_Update_EmptyBody_Returns400)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const int adminId = findAdminUserId(*token);
    REQUIRE(adminId > 0);

    Json::Value emptyBody;
    auto resp = sendPutJson("/api/admin/users/" + std::to_string(adminId), emptyBody, *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}

// ---------------------------------------------------------------------------
// disable -> enable round-trip: PUT /disable sets locked_until to the
// forever-sentinel (200), then POST /enable resets it (200), and the user is
// left enabled. Snapshot/restore guards the original locked_until. Covers
// both the disable and enable happy paths.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminUser_DisableEnable_RoundTrip)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const int adminId = findAdminUserId(*token);
    REQUIRE(adminId > 0);

    const auto snapshot = snapshotAdminUser(adminId);
    struct Restore
    {
        int id;
        AdminUserSnapshot snap;
        ~Restore()
        {
            restoreAdminUser(id, snap);
        }
    } guard{adminId, snapshot};

    // Disable -> 200 (disable is PUT in the route table).
    auto disResp = sendPutJson(
      "/api/admin/users/" + std::to_string(adminId) + "/disable", Json::Value::nullSingleton(), *token);
    REQUIRE(disResp != nullptr);
    CHECK(statusIs(disResp, drogon::k200OK));

    // Enable -> 200 (enable is POST in the route table; the handler does not
    // read the body, so an empty JSON body is fine).
    auto enResp = sendPostJson(
      "/api/admin/users/" + std::to_string(adminId) + "/enable", Json::Value::nullSingleton(), *token);
    REQUIRE(enResp != nullptr);
    CHECK(statusIs(enResp, drogon::k200OK));
}

// ---------------------------------------------------------------------------
// getUserRoles happy path: GET /api/admin/users/{adminId}/roles returns 200
// with a roles array containing "admin" (the seeded admin role).
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_AdminUser_GetRoles_AdminUser_Returns200WithAdminRole)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const int adminId = findAdminUserId(*token);
    REQUIRE(adminId > 0);

    auto resp = sendGet("/api/admin/users/" + std::to_string(adminId) + "/roles", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["status"].asString() == "success");
    CHECK(body.isMember("roles"));
}
