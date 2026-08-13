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
// Test-data isolation: createUser / updateUser-expanded tests create users
// with unique timestamp-based suffixes so they don't collide across runs.
// The legacy cases (update/disable/enable on the seeded admin) still use
// the RAII snapshot/restore pattern.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "HttpTestClient.h"

#include <chrono>
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
    // The list endpoint is paginated (default 50/page); use ?q=admin so the
    // seeded admin user is found regardless of which page it falls on.
    auto resp = sendGet("/api/admin/users?q=admin", token);
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

// ---------------------------------------------------------------------------
// Unique-suffix helper for createUser / updateUser-expanded tests (each test
// creates its own throwaway users; no delete endpoint yet).
// ---------------------------------------------------------------------------
namespace
{
std::string uniqueSuffix()
{
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::to_string(now % 1000000);
}
}  // namespace

// ---------------------------------------------------------------------------
// createUser happy path: POST /api/admin/users with username+password returns
// 201, and the new user is retrievable via GET. The created user has a unique
// suffix so repeated runs don't collide on the UNIQUE constraint.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_AdminUser_Create_ValidBody_Returns201)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto suffix = uniqueSuffix();
    Json::Value body;
    body["username"] = "crudtest_" + suffix;
    body["password"] = "TestPass123!";
    body["email"] = ("crudtest_" + suffix + "@example.com");

    auto resp = sendPostJson("/api/admin/users", body, *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k201Created));
    Json::Value respBody;
    REQUIRE(parseJsonBody(resp, respBody));
    CHECK(respBody["status"].asString() == "success");
    CHECK(respBody["user"]["username"].asString() == ("crudtest_" + suffix));

    // Verify the new user is retrievable.
    int newId = respBody["user"]["id"].asInt();
    auto getResp = sendGet("/api/admin/users/" + std::to_string(newId), *token);
    REQUIRE(getResp != nullptr);
    CHECK(statusIs(getResp, drogon::k200OK));
}

// ---------------------------------------------------------------------------
// createUser duplicate-username: POST with an existing username returns 400.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminUser_Create_DuplicateUsername_Returns409)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    // First create succeeds.
    auto suffix = uniqueSuffix();
    Json::Value body;
    body["username"] = "duptest_" + suffix;
    body["password"] = "TestPass123!";
    auto resp1 = sendPostJson("/api/admin/users", body, *token);
    REQUIRE(resp1 != nullptr);
    CHECK(statusIs(resp1, drogon::k201Created));

    // Second create with same username → VALIDATION_USERNAME_TAKEN (HTTP 409).
    auto resp2 = sendPostJson("/api/admin/users", body, *token);
    REQUIRE(resp2 != nullptr);
    CHECK(statusIs(resp2, drogon::k409Conflict));
}

// ---------------------------------------------------------------------------
// createUser missing-fields: POST without username returns 400.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminUser_Create_MissingUsername_Returns400)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    Json::Value body;
    body["password"] = "TestPass123!";

    auto resp = sendPostJson("/api/admin/users", body, *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}

// ---------------------------------------------------------------------------
// listUsers pagination: create two users with a unique prefix, then GET with
// ?q=<prefix>&per_page=1 to verify total/total_pages/page fields.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_AdminUser_List_Pagination)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    // Create two users with a shared prefix for isolation.
    auto suffix = uniqueSuffix();
    std::string prefix = "pagtest_" + suffix;
    for (int i = 0; i < 2; ++i)
    {
        Json::Value body;
        body["username"] = prefix + "_" + std::to_string(i);
        body["password"] = "TestPass123!";
        auto cr = sendPostJson("/api/admin/users", body, *token);
        REQUIRE(cr != nullptr);
        CHECK(statusIs(cr, drogon::k201Created));
    }

    // Page 1 with per_page=1 → total >= 2, total_pages >= 2, 1 user on page.
    auto resp = sendGet("/api/admin/users?q=" + prefix + "&per_page=1&page=1", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["per_page"].asInt() == 1);
    CHECK(body["total"].asInt() >= 2);
    CHECK(body["total_pages"].asInt() >= 2);
    CHECK(body["users"].size() <= 1);
}

// ---------------------------------------------------------------------------
// listUsers search: create a user with a unique prefix, verify ?q= finds it.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminUser_List_Search_ByQ)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto suffix = uniqueSuffix();
    Json::Value body;
    body["username"] = "srchtest_" + suffix;
    body["password"] = "TestPass123!";
    auto cr = sendPostJson("/api/admin/users", body, *token);
    REQUIRE(cr != nullptr);
    CHECK(statusIs(cr, drogon::k201Created));

    auto resp = sendGet("/api/admin/users?q=srchtest_" + suffix, *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value respBody;
    REQUIRE(parseJsonBody(resp, respBody));
    CHECK(respBody["total"].asInt() >= 1);
    // The created user must appear in the filtered results.
    bool found = false;
    for (const auto &u : respBody["users"])
    {
        if (u.get("username", "").asString() == ("srchtest_" + suffix))
        {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ---------------------------------------------------------------------------
// updateUser expanded fields: create a user, then PUT with mfa_enabled and
// locked, verify the changes persist via GET.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminUser_Update_ExpandedFields)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    // Create a throwaway user.
    auto suffix = uniqueSuffix();
    Json::Value createBody;
    createBody["username"] = "updtest_" + suffix;
    createBody["password"] = "TestPass123!";
    auto cr = sendPostJson("/api/admin/users", createBody, *token);
    REQUIRE(cr != nullptr);
    REQUIRE(statusIs(cr, drogon::k201Created));
    Json::Value crBody;
    REQUIRE(parseJsonBody(cr, crBody));
    int userId = crBody["user"]["id"].asInt();

    // Update mfa_enabled + locked.
    Json::Value updBody;
    updBody["mfa_enabled"] = true;
    updBody["locked"] = true;
    auto updResp = sendPutJson("/api/admin/users/" + std::to_string(userId), updBody, *token);
    REQUIRE(updResp != nullptr);
    CHECK(statusIs(updResp, drogon::k200OK));

    // Verify via GET.
    auto getResp = sendGet("/api/admin/users/" + std::to_string(userId), *token);
    REQUIRE(getResp != nullptr);
    Json::Value body;
    REQUIRE(parseJsonBody(getResp, body));
    CHECK(body["mfa_enabled"].asBool() == true);
    CHECK(body["locked"].asBool() == true);
}

// ---------------------------------------------------------------------------
// listUsers locked filter: the user created+locked above should be findable
// with ?locked=true. This is a standalone case (creates its own locked user).
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_AdminUser_List_FilterLocked)
{
    ADMIN_USER_SKIP_GUARD;

    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    // Create + lock a user.
    auto suffix = uniqueSuffix();
    Json::Value createBody;
    createBody["username"] = "locktest_" + suffix;
    createBody["password"] = "TestPass123!";
    auto cr = sendPostJson("/api/admin/users", createBody, *token);
    REQUIRE(cr != nullptr);
    REQUIRE(statusIs(cr, drogon::k201Created));
    Json::Value crBody;
    REQUIRE(parseJsonBody(cr, crBody));
    int userId = crBody["user"]["id"].asInt();

    Json::Value lockBody;
    lockBody["locked"] = true;
    sendPutJson("/api/admin/users/" + std::to_string(userId), lockBody, *token);

    // The locked user should appear in ?locked=true&q=locktest_<suffix>.
    auto resp = sendGet("/api/admin/users?q=locktest_" + suffix + "&locked=true", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["total"].asInt() >= 1);
}
