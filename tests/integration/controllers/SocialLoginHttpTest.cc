// tests/integration/controllers/SocialLoginHttpTest.cc
//
// Mock-based HTTP integration tests for the Social OAuth controllers
// (libs/drogon/src/controllers/{Google,WeChat,GitHub}Controller.cc). These
// controllers were previously 5.9-16.3% covered (only incidental startup
// hits). Each `/api/{provider}/login` route delegates to an injected
// XxxAuthService; tests/common/SocialMockFixture.h installs a real service
// backed by FakeOAuthHttpClient (and FakeSocialAccountRepository for GitHub)
// onto the controller singleton via DrClassMap::getSingleInstance, so the
// controller's injected path runs WITHOUT any real outbound network.
//
// Layer note: these COMPLEMENT (not duplicate) libs/identity/test/
// SocialAuthServiceTest.cc -- that file tests the SERVICE result structs;
// these tests cover the CONTROLLER layer (request body parsing, the
// `if(service_)` injection branch, response re-filtering into JSON, and the
// ErrorResponder error-envelope path with HTTP status codes).
//
// Storage: these run in EVERY CI leg (memory-mode included). The injected
// Google/WeChat paths do NO DB writes (they only filter the service result
// into JSON). GitHub's injected path calls issueTokensForUser on SUCCESS --
// which under memory mode assert-crashes the process (review B1) -- so GitHub
// cases here configure the fake to return ERRORS only (no success queue). No
// postgresAvailable() guard needed for the error-only paths.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include "HttpTestClient.h"
#include "SocialMockFixture.h"

#include <string>

using authforge::test::http::parseJsonBody;
using authforge::test::http::sendPostForm;
using authforge::test::http::serverReachable;
using authforge::test::http::statusIs;
using authforge::test::social::injectGitHubFake;
using authforge::test::social::injectGoogleFake;
using authforge::test::social::injectWeChatFake;

// Server-reachability guard (these routes need no DB, but the in-process
// server must be up). No postgresAvailable() guard: the mock-injected paths
// run under memory mode too.
#define SOCIAL_SKIP_GUARD                                    \
    do                                                       \
    {                                                        \
        if (!serverReachable())                              \
        {                                                    \
            CHECK(true);                                     \
            return;                                          \
        }                                                    \
    } while (0)

// ===========================================================================
// Google (/api/google/login)
// ===========================================================================

// Happy path: fake token exchange + userinfo, controller re-filters to
// {sub,name,email,picture} and drops extra fields. Covers the injected-path
// success branch + the controller's JSON response shaping.
DROGON_TEST(Integration_P0_GoogleLogin_FakeExchange_ReturnsFilteredProfile)
{
    SOCIAL_SKIP_GUARD;

    auto http = injectGoogleFake();
    Json::Value tokenBody;
    tokenBody["access_token"] = "gtok-test";
    http->postFormResponses.push_back(
      authforge::identity::testing::okJson(tokenBody));
    Json::Value userBody;
    userBody["sub"] = "g-123";
    userBody["name"] = "Test User";
    userBody["email"] = "test@example.com";
    userBody["picture"] = "https://example.test/pic.png";
    userBody["extra_field_should_be_dropped"] = "secret";
    http->getResponses.push_back(authforge::identity::testing::okJson(userBody));

    auto resp = sendPostForm("/api/google/login", "code=test-auth-code");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["sub"].asString() == "g-123");
    CHECK(body["name"].asString() == "Test User");
    CHECK(body["email"].asString() == "test@example.com");
    CHECK(body["picture"].asString() == "https://example.test/pic.png");
    // The controller's filter drops any field not in {sub,name,email,picture}.
    CHECK(!body.isMember("extra_field_should_be_dropped"));
}

// Missing code -> 400 VALIDATION_MISSING_REQUIRED_FIELD (controller's own
// validation, before the service is called). The controller's body parser
// scans for the literal "code=" substring, so the body must NOT contain that
// substring anywhere (e.g. "state=xyz" is safe; "notcode=xyz" is NOT, because
// it contains "code=").
DROGON_TEST(Integration_P1_GoogleLogin_MissingCode_Returns400)
{
    SOCIAL_SKIP_GUARD;

    injectGoogleFake();  // install fake (not actually hit on this path)
    auto resp = sendPostForm("/api/google/login", "state=xyz&other=val");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}

// Transport failure (token-exchange HTTP call fails) -> service returns
// NET_CONNECTION_FAILED -> controller responds with the NETWORK error envelope
// (HTTP 502, NETWORK-category default).
DROGON_TEST(Integration_P1_GoogleLogin_TransportFailure_Returns502)
{
    SOCIAL_SKIP_GUARD;

    auto http = injectGoogleFake();
    http->postFormResponses.push_back(authforge::identity::testing::transportFailure());

    auto resp = sendPostForm("/api/google/login", "code=test-auth-code");
    REQUIRE(resp != nullptr);
    // NET_CONNECTION_FAILED is NETWORK-category -> 502 (ErrorTypes.cc default).
    CHECK(statusIs(resp, drogon::k502BadGateway));
}

// ===========================================================================
// WeChat (/api/wechat/login)
// ===========================================================================

// Happy path: fake access_token response + userinfo, controller re-filters to
// {openid, nickname, headimgurl, sex, city, province, country}.
DROGON_TEST(Integration_P0_WeChatLogin_FakeExchange_ReturnsFilteredProfile)
{
    SOCIAL_SKIP_GUARD;

    auto http = injectWeChatFake();
    Json::Value tokenBody;
    tokenBody["access_token"] = "wtok-test";
    tokenBody["openid"] = "wx-openid-1";
    http->getResponses.push_back(authforge::identity::testing::okJson(tokenBody));
    Json::Value userBody;
    userBody["openid"] = "wx-openid-1";
    userBody["nickname"] = "WX User";
    userBody["headimgurl"] = "https://example.test/wx.png";
    userBody["sex"] = 1;
    userBody["city"] = "Shanghai";
    userBody["province"] = "Shanghai";
    userBody["country"] = "CN";
    userBody["privilege_should_be_dropped"] = "x";
    http->getResponses.push_back(authforge::identity::testing::okJson(userBody));

    auto resp = sendPostForm("/api/wechat/login", "code=wx-auth-code");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body["openid"].asString() == "wx-openid-1");
    CHECK(body["nickname"].asString() == "WX User");
    CHECK(body.isMember("headimgurl"));
}

// Missing code -> 400. Body must not contain the "code=" substring (same
// parser quirk as Google's missing-code case above).
DROGON_TEST(Integration_P1_WeChatLogin_MissingCode_Returns400)
{
    SOCIAL_SKIP_GUARD;

    injectWeChatFake();
    auto resp = sendPostForm("/api/wechat/login", "state=xyz&other=val");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}

// ===========================================================================
// GitHub (/api/github/login)
// ===========================================================================
//
// GitHubController::issueTokensForUser now routes token issuance through
// OAuth2Plugin::saveTokenPair (the storage abstraction) instead of calling
// drogon::app().getDbClient() directly. The old direct path crashed the
// process under memory storage via an uncatchable getDbClient() assert
// (review B1), so the GitHub happy-path was previously untestable. With the
// saveTokenPair fix, the happy-path runs in BOTH memory and Postgres modes
// (saveTokenPair -> MemoryTokenRepository in memory mode, which is DB-free).

// Happy path: fake token exchange + userinfo, FakeSocialAccountRepository
// pre-seeded with a linked user, controller mints tokens via
// plugin->saveTokenPair and returns {access_token, refresh_token, token_type,
// expires_in}. Runs in memory mode (the GitHub blocker is resolved).
DROGON_TEST(Integration_P0_GitHubLogin_FakeExchange_ReturnsTokens)
{
    SOCIAL_SKIP_GUARD;

    auto h = injectGitHubFake();
    // Token exchange response.
    Json::Value tokenBody;
    tokenBody["access_token"] = "gh-tok-test";
    tokenBody["token_type"] = "bearer";
    tokenBody["scope"] = "user";
    h.http->postFormResponses.push_back(authforge::identity::testing::okJson(tokenBody));
    // Userinfo response.
    Json::Value userBody;
    userBody["id"] = 12345;
    userBody["login"] = "gh-test-user";
    userBody["email"] = "gh@example.com";
    userBody["name"] = "GH Test";
    h.http->getResponses.push_back(authforge::identity::testing::okJson(userBody));

    auto resp = sendPostForm("/api/github/login", "code=gh-auth-code");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body.isMember("access_token"));
    CHECK(body.isMember("refresh_token"));
    CHECK(body["token_type"].asString() == "Bearer");
    CHECK(body.isMember("expires_in"));
}

// Missing code -> 400 (controller validation, service not called).
DROGON_TEST(Integration_P1_GitHubLogin_MissingCode_Returns400)
{
    SOCIAL_SKIP_GUARD;

    injectGitHubFake();
    auto resp = sendPostForm("/api/github/login", "state=xyz");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}

// Transport failure on token exchange -> service returns NET_CONNECTION_FAILED
// -> controller error envelope (502). Safe under memory mode because the
// service returns an error, never reaching issueTokensForUser.
DROGON_TEST(Integration_P1_GitHubLogin_TransportFailure_Returns502)
{
    SOCIAL_SKIP_GUARD;

    auto h = injectGitHubFake();
    h.http->postFormResponses.push_back(authforge::identity::testing::transportFailure());

    auto resp = sendPostForm("/api/github/login", "code=gh-auth-code");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k502BadGateway));
}

// #54 (V024 soft-delete contract): a mapping that resolves to a
// soft-deleted/locked user answers AccountUnavailable -> generic 401 with NO
// access_token issued (previously tokens were minted unconditionally). Error
// path only, so it is safe under memory mode.
DROGON_TEST(Integration_P0_GitHubLogin_DeletedLinkedUser_Rejected401)
{
    SOCIAL_SKIP_GUARD;

    auto h = injectGitHubFake();
    // Mark (github, "12345") as linked-but-unavailable (soft-deleted/locked).
    h.accountRepo->unavailableKeys.insert(
      authforge::identity::testing::FakeSocialAccountRepository::key("github", "12345")
    );
    Json::Value tokenBody;
    tokenBody["access_token"] = "gh-tok-test";
    tokenBody["token_type"] = "bearer";
    h.http->postFormResponses.push_back(authforge::identity::testing::okJson(tokenBody));
    Json::Value userBody;
    userBody["id"] = 12345;
    userBody["login"] = "gh-test-user";
    userBody["email"] = "gh@example.com";
    h.http->getResponses.push_back(authforge::identity::testing::okJson(userBody));

    auto resp = sendPostForm("/api/github/login", "code=gh-auth-code");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(!body.isMember("access_token"));
}

// #54 (review F1): a derived username held by an existing row (active or
// soft-deleted) makes createLinkedUser fail closed (ON CONFLICT DO NOTHING —
// no row adoption/account takeover) -> DB_QUERY_ERROR, no tokens.
DROGON_TEST(Integration_P0_GitHubLogin_ConflictingUsername_NoAdoption)
{
    SOCIAL_SKIP_GUARD;

    auto h = injectGitHubFake();
    // No mapping for the subject, but "gh_gh-test-user" (the derived
    // username) already exists.
    h.accountRepo->conflictingUsernames.insert("gh_gh-test-user");
    Json::Value tokenBody;
    tokenBody["access_token"] = "gh-tok-test";
    tokenBody["token_type"] = "bearer";
    h.http->postFormResponses.push_back(authforge::identity::testing::okJson(tokenBody));
    Json::Value userBody;
    userBody["id"] = 54321;
    userBody["login"] = "gh-test-user";
    userBody["email"] = "gh@example.com";
    h.http->getResponses.push_back(authforge::identity::testing::okJson(userBody));

    auto resp = sendPostForm("/api/github/login", "code=gh-auth-code");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k500InternalServerError));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(!body.isMember("access_token"));
}
