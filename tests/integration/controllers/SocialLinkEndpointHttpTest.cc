// tests/integration/controllers/SocialLinkEndpointHttpTest.cc
//
// B2 social account link/unlink: HTTP integration tests for the
// /api/me/social/links* routes (UserSelfServiceController, design doc §3).
// The SocialLinkService is mock-injected via SocialMockFixture's
// injectSocialLinkFake() (fake http + fake mapping repo), so no provider
// network and no oauth2_subject_mappings DB writes happen -- Postgres is
// only needed for the controller's user resolution and the bearer-token
// flow, hence every case is PG-guarded (same contract as
// UserSelfServiceEndpointHttpTest: memory mode cannot produce a filter-valid
// token for /api/me).
//
// Route map (UserSelfServiceController.h, WITH_SOCIAL):
//   GET    /api/me/social/links             -> listSocialLinks
//   POST   /api/me/social/links/{provider}  -> linkSocialAccount
//   DELETE /api/me/social/links/{provider}  -> unlinkSocialAccount

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include "HttpTestClient.h"
#include "SocialMockFixture.h"

#include <string>

using fulla::test::http::loginAsAdmin;
using fulla::test::http::parseJsonBody;
using fulla::test::http::postgresAvailable;
using fulla::test::http::sendDelete;
using fulla::test::http::sendGet;
using fulla::test::http::sendPostJson;
using fulla::test::http::serverReachable;
using fulla::test::http::statusIs;
using fulla::test::social::injectSocialLinkFake;

#define SOCIALLINK_SKIP_GUARD                                   \
    do                                                          \
    {                                                           \
        if (!postgresAvailable() || !serverReachable())         \
        {                                                       \
            CHECK(true);                                        \
            return;                                             \
        }                                                       \
    } while (0)

// Seed the fake for a successful GitHub exchange resolving to subject 4242.
static void queueGithubOk(const fulla::test::social::SocialLinkFakeHandle &h, int64_t id)
{
    Json::Value tokenBody;
    tokenBody["access_token"] = "ghtok";
    h.http->postFormResponses.push_back(
      fulla::identity::testing::okJson(tokenBody));
    Json::Value userBody;
    userBody["id"] = id;
    userBody["login"] = "octocat";
    h.http->getResponses.push_back(fulla::identity::testing::okJson(userBody));
}

// POST body {"code": "..."} (pre-validation tests: rejected before the
// state gate parses anything).
static Json::Value codeBody(const char *code)
{
    Json::Value body;
    body["code"] = code;
    return body;
}

// POST body {"code": "...", "state": "..."} -- the real link request (#71).
static Json::Value linkBody(const char *code, const std::string &state)
{
    Json::Value body;
    body["code"] = code;
    body["state"] = state;
    return body;
}

// The link state must be bound to the caller's internal id (#71). Resolve the
// seeded admin's id through the admin API (same technique as
// UserAdminHardeningTest's LastAdminGuard).
static int32_t adminInternalId(const std::string &token)
{
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

// Mint a state bound to (admin, provider) through the injected fake's store.
static std::string mintAdminState(
  const fulla::test::social::SocialLinkFakeHandle &h,
  const std::string &token,
  const std::string &provider)
{
    return h.mintState(adminInternalId(token), provider);
}

// ---------------------------------------------------------------------------
// Auth guard: every route sits behind OAuth2AuthFilter.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_SocialLink_NoToken_Returns401)
{
    SOCIALLINK_SKIP_GUARD;
    injectSocialLinkFake();

    CHECK(statusIs(sendGet("/api/me/social/links"), drogon::k401Unauthorized));
    CHECK(statusIs(
      sendPostJson("/api/me/social/links/github", codeBody("c")),
      drogon::k401Unauthorized
    ));
    CHECK(statusIs(sendDelete("/api/me/social/links/github"), drogon::k401Unauthorized));
}

// ---------------------------------------------------------------------------
// Validation: unsupported provider -> 400; missing code -> 400.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_SocialLink_BadProvider_Returns400)
{
    SOCIALLINK_SKIP_GUARD;
    injectSocialLinkFake();
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendPostJson("/api/me/social/links/facebook", codeBody("c"), *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));

    auto delResp = sendDelete("/api/me/social/links/facebook", *token);
    REQUIRE(delResp != nullptr);
    CHECK(statusIs(delResp, drogon::k400BadRequest));
}

DROGON_TEST(Integration_P1_SocialLink_MissingCode_Returns400)
{
    SOCIALLINK_SKIP_GUARD;
    injectSocialLinkFake();
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendPostJson("/api/me/social/links/github", Json::Value(Json::objectValue), *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}

// ---------------------------------------------------------------------------
// Happy path: link github -> list shows the entry.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_SocialLink_LinkGithubThenList_ShowsEntry)
{
    SOCIALLINK_SKIP_GUARD;
    auto h = injectSocialLinkFake();
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    queueGithubOk(h, 4242);
    auto linkResp = sendPostJson(
      "/api/me/social/links/github", linkBody("c1", mintAdminState(h, *token, "github")), *token);
    REQUIRE(linkResp != nullptr);
    CHECK(statusIs(linkResp, drogon::k200OK));
    Json::Value linkBody;
    REQUIRE(parseJsonBody(linkResp, linkBody));
    CHECK(linkBody["provider"].asString() == "github");
    CHECK(linkBody["subject"].asString() == "4242");

    auto listResp = sendGet("/api/me/social/links", *token);
    REQUIRE(listResp != nullptr);
    CHECK(statusIs(listResp, drogon::k200OK));
    Json::Value listBody;
    REQUIRE(parseJsonBody(listResp, listBody));
    CHECK(listBody["total"].asInt() == 1);
    CHECK(listBody["social_links"][0]["provider"].asString() == "github");
    CHECK(listBody["social_links"][0]["subject"].asString() == "4242");
}

// ---------------------------------------------------------------------------
// Re-linking the same (provider, subject) -> 409 (AlreadyLinkedToSelf).
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_SocialLink_RelinkSameSubject_Returns409)
{
    SOCIALLINK_SKIP_GUARD;
    auto h = injectSocialLinkFake();
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    queueGithubOk(h, 4242);
    CHECK(statusIs(
      sendPostJson(
        "/api/me/social/links/github", linkBody("c1", mintAdminState(h, *token, "github")), *token),
      drogon::k200OK
    ));
    queueGithubOk(h, 4242);
    auto resp = sendPostJson(
      "/api/me/social/links/github", linkBody("c2", mintAdminState(h, *token, "github")), *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k409Conflict));
}

// ---------------------------------------------------------------------------
// The provider identity already belongs to another user -> 409, fixed
// wording (no owner information).
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_SocialLink_SubjectOwnedByOtherUser_Returns409)
{
    SOCIALLINK_SKIP_GUARD;
    auto h = injectSocialLinkFake();
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    fulla::identity::SocialAccountLookup other;
    other.userId = 999999;
    other.username = "someone-else";
    h.accountRepo
      ->linked[fulla::identity::testing::FakeSocialAccountRepository::key("github", "4242")] =
      other;
    queueGithubOk(h, 4242);

    auto resp = sendPostJson(
      "/api/me/social/links/github", linkBody("c1", mintAdminState(h, *token, "github")), *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k409Conflict));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    // Error envelope shape. (No bare `||` inside CHECK: drogon's Decomposer
    // macro re-associates unparenthesized boolean operators -- see
    // FunctionalTest.cc's `(bool)(...)` precedent.)
    CHECK(body["error"]["code"].asString() == "VALIDATION_RESOURCE_CONFLICT");
}

// ---------------------------------------------------------------------------
// Provider exchange failure -> 502 (NET_CONNECTION_FAILED via the catalog).
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_SocialLink_ExchangeFailure_Returns502)
{
    SOCIALLINK_SKIP_GUARD;
    auto h = injectSocialLinkFake();
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    h.http->postFormResponses.push_back(fulla::identity::testing::transportFailure());
    auto resp = sendPostJson(
      "/api/me/social/links/github", linkBody("bad", mintAdminState(h, *token, "github")), *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k502BadGateway));
}

// ---------------------------------------------------------------------------
// Unlink: no mapping -> 404.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_SocialUnlink_NoLink_Returns404)
{
    SOCIALLINK_SKIP_GUARD;
    injectSocialLinkFake();
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendDelete("/api/me/social/links/github", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k404NotFound));
}

// ---------------------------------------------------------------------------
// Last-credential guard: single link + no usable password (the fake's
// default) -> 409, mapping kept.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_SocialUnlink_LastLinkNoPassword_Returns409)
{
    SOCIALLINK_SKIP_GUARD;
    auto h = injectSocialLinkFake();
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    queueGithubOk(h, 4242);
    CHECK(statusIs(
      sendPostJson(
        "/api/me/social/links/github", linkBody("c1", mintAdminState(h, *token, "github")), *token),
      drogon::k200OK
    ));

    auto resp = sendDelete("/api/me/social/links/github", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k409Conflict));

    // The mapping row survived the refused unlink.
    auto listResp = sendGet("/api/me/social/links", *token);
    REQUIRE(listResp != nullptr);
    Json::Value listBody;
    REQUIRE(parseJsonBody(listResp, listBody));
    CHECK(listBody["total"].asInt() == 1);
}

// ---------------------------------------------------------------------------
// Guard passes with a usable password: mark the resolved internal id as
// password-capable in the fake (read back from the inserted mapping), then
// unlink -> 200 and the list is empty again.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_SocialUnlink_WithPassword_Succeeds)
{
    SOCIALLINK_SKIP_GUARD;
    auto h = injectSocialLinkFake();
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    queueGithubOk(h, 4242);
    CHECK(statusIs(
      sendPostJson(
        "/api/me/social/links/github", linkBody("c1", mintAdminState(h, *token, "github")), *token),
      drogon::k200OK
    ));

    // The fake's inserted mapping carries the controller-resolved internal id.
    auto it = h.accountRepo
                 ->linked.find(
                   fulla::identity::testing::FakeSocialAccountRepository::key("github", "4242")
                 );
    REQUIRE(it != h.accountRepo->linked.end());
    h.accountRepo->usersWithUsablePassword.insert(it->second.userId);

    auto resp = sendDelete("/api/me/social/links/github", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));

    auto listResp = sendGet("/api/me/social/links", *token);
    REQUIRE(listResp != nullptr);
    Json::Value listBody;
    REQUIRE(parseJsonBody(listResp, listBody));
    CHECK(listBody["total"].asInt() == 0);
}

// ---------------------------------------------------------------------------
// With a second link present the guard does not apply: unlink github -> 200,
// google's link remains.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_SocialUnlink_SecondLinkPresent_NoGuard)
{
    SOCIALLINK_SKIP_GUARD;
    auto h = injectSocialLinkFake();
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    queueGithubOk(h, 4242);
    CHECK(statusIs(
      sendPostJson(
        "/api/me/social/links/github", linkBody("c1", mintAdminState(h, *token, "github")), *token),
      drogon::k200OK
    ));
    Json::Value gToken;
    gToken["access_token"] = "gtok";
    h.http->postFormResponses.push_back(fulla::identity::testing::okJson(gToken));
    Json::Value gUser;
    gUser["sub"] = "google-sub-1";
    h.http->getResponses.push_back(fulla::identity::testing::okJson(gUser));
    CHECK(statusIs(
      sendPostJson(
        "/api/me/social/links/google", linkBody("c2", mintAdminState(h, *token, "google")), *token),
      drogon::k200OK
    ));

    auto resp = sendDelete("/api/me/social/links/github", *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));

    auto listResp = sendGet("/api/me/social/links", *token);
    REQUIRE(listResp != nullptr);
    Json::Value listBody;
    REQUIRE(parseJsonBody(listResp, listBody));
    CHECK(listBody["total"].asInt() == 1);
    CHECK(listBody["social_links"][0]["provider"].asString() == "google");
}

// ---------------------------------------------------------------------------
// #71: the state gate over HTTP.
// ---------------------------------------------------------------------------

// A link POST without state is a 400 (VALIDATION_MISSING_REQUIRED_FIELD) --
// the stateless injection surface is closed.
DROGON_TEST(Integration_P1_SocialLink_MissingState_Returns400)
{
    SOCIALLINK_SKIP_GUARD;
    auto h = injectSocialLinkFake();
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    auto resp = sendPostJson("/api/me/social/links/github", codeBody("c1"), *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["error"]["code"].asString() == "VALIDATION_MISSING_REQUIRED_FIELD");
}

// A state bound to a DIFFERENT internal user is rejected (envelope 400,
// generic wording -- no oracle about which check failed).
DROGON_TEST(Integration_P1_SocialLink_StateBoundToOtherUser_Returns400)
{
    SOCIALLINK_SKIP_GUARD;
    auto h = injectSocialLinkFake();
    auto token = loginAsAdmin();
    REQUIRE(token.has_value());

    const std::string foreignState = h.mintState(999999, "github");
    queueGithubOk(h, 4242);
    auto resp = sendPostJson("/api/me/social/links/github", linkBody("c1", foreignState), *token);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}

// The authorize endpoint sits behind the auth filter like its siblings.
DROGON_TEST(Integration_P1_SocialLink_Authorize_NoToken_Returns401)
{
    SOCIALLINK_SKIP_GUARD;
    injectSocialLinkFake();
    CHECK(statusIs(
      sendPostJson("/api/me/social/links/github/authorize", Json::Value(Json::objectValue)),
      drogon::k401Unauthorized
    ));
}
