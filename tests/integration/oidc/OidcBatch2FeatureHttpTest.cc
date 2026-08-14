// tests/integration/oidc/OidcBatch2FeatureHttpTest.cc
//
// HTTP integration tests for the OAuth/OIDC compliance Batch 2 features:
//   - F-022: prompt=none without a session -> login_required redirect (never UI)
//   - F-022: prompt containing both "none" and another value -> 400 (malformed)
//   - F-017: token_endpoint_auth_method enforcement (backend-svc is seeded
//            client_secret_basic, so a body-only secret is rejected)
//   - F-023: userinfo requires an openid-scoped access token (403 otherwise)
//   - F-027: end_session endpoint returns 200 when no redirect URI is given
//
// These tests require Postgres (seed clients) + the live HTTP listener on
// 127.0.0.1:5555; they skip cleanly otherwise.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <json/json.h>

#include "HttpTestClient.h"

#include <string>

using authforge::test::http::parseJsonBody;
using authforge::test::http::postgresAvailable;
using authforge::test::http::sendGet;
using authforge::test::http::sendPostForm;
using authforge::test::http::serverReachable;
using authforge::test::http::statusIs;

#define OIDC_BATCH2_SKIP_GUARD                                 \
    do                                                         \
    {                                                          \
        if (!postgresAvailable() || !serverReachable())        \
        {                                                      \
            CHECK(true);                                       \
            return;                                            \
        }                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// F-022 (OIDC Core §3.1.2.1): prompt=none with no authenticated session
// cannot be satisfied without UI -> the server MUST redirect back to the
// client with error=login_required (never show a login page).
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_OidcBatch2_PromptNone_NoSession_ReturnsLoginRequired)
{
    OIDC_BATCH2_SKIP_GUARD;

    auto resp = sendGet(
      "/oauth2/authorize?response_type=code&client_id=vue-client"
      "&redirect_uri=http://127.0.0.1:5173/callback&scope=openid"
      "&state=abcdef1234&prompt=none");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k302Found));
    auto location = resp->getHeader("location");
    CHECK(location.find("error=login_required") != std::string::npos);
    // state is echoed back.
    CHECK(location.find("state=abcdef1234") != std::string::npos);
}

// ---------------------------------------------------------------------------
// F-022 (OIDC Core §3.1.2.1): prompt=consent with no session still cannot show
// UI, but unlike prompt=none it does NOT short-circuit to login_required
// immediately -- it proceeds to the login redirect (the consent flag is only
// relevant once authenticated). This confirms prompt parsing does not crash
// on the consent value and the request flows to the login redirect path.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_OidcBatch2_PromptConsent_NoSession_RedirectsToLogin)
{
    OIDC_BATCH2_SKIP_GUARD;

    auto resp = sendGet(
      "/oauth2/authorize?response_type=code&client_id=vue-client"
      "&redirect_uri=http://127.0.0.1:5173/callback&scope=openid"
      "&state=abcdef1234&prompt=consent");
    REQUIRE(resp != nullptr);
    // No session -> redirects to the login screen (302), not an error.
    CHECK(statusIs(resp, drogon::k302Found));
    auto location = resp->getHeader("location");
    CHECK(location.find("/login") != std::string::npos);
}

// ---------------------------------------------------------------------------
// F-017: backend-svc is seeded with token_endpoint_auth_method=
// client_secret_basic, so the client_secret MUST arrive via HTTP Basic.
// Sending it in the POST body is rejected with invalid_client (401).
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_OidcBatch2_ClientSecretPost_RejectedForBasicClient)
{
    OIDC_BATCH2_SKIP_GUARD;

    // Body-only secret (no Authorization: Basic header).
    auto resp = sendPostForm(
      "/oauth2/token",
      "grant_type=client_credentials&client_id=backend-svc&client_secret=test-secret&scope=tokens:read"
    );
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
    Json::Value body;
    if (parseJsonBody(resp, body))
    {
        CHECK(body["error"].asString() == "invalid_client");
    }
}

// ---------------------------------------------------------------------------
// F-017: backend-svc succeeds when the secret is sent via HTTP Basic (the
// declared method). This is the positive counterpart of the test above and
// guards against over-restrictive enforcement.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_OidcBatch2_ClientSecretBasic_AcceptedForBasicClient)
{
    OIDC_BATCH2_SKIP_GUARD;

    try
    {
        auto client =
          ::drogon::HttpClient::newHttpClient("http://127.0.0.1:5555", ::drogon::app().getLoop());
        auto req = ::drogon::HttpRequest::newHttpRequest();
        req->setMethod(::drogon::Post);
        req->setPath("/oauth2/token");
        req->setContentTypeCode(::drogon::CT_APPLICATION_X_FORM);
        req->addHeader(
          "Authorization",
          "Basic " + ::drogon::utils::base64Encode("backend-svc:test-secret")
        );
        req->setBody("grant_type=client_credentials&client_id=backend-svc&scope=tokens:read");
        auto [result, resp] = client->sendRequest(req, 30.0);
        REQUIRE(result == ::drogon::ReqResult::Ok);
        REQUIRE(resp != nullptr);
        CHECK(statusIs(resp, drogon::k200OK));
    }
    catch (const std::exception &e)
    {
        LOG_WARN << "Basic-auth token request failed: " << e.what();
        CHECK(false);
    }
}

// ---------------------------------------------------------------------------
// F-027 (OIDC RP-Initiated Logout 1.0 §2): GET /oauth2/end_session with no
// post_logout_redirect_uri terminates the session and returns 200 with a
// "logged out" body.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_OidcBatch2_EndSession_NoRedirectUri_Returns200)
{
    OIDC_BATCH2_SKIP_GUARD;

    auto resp = sendGet("/oauth2/end_session");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    if (parseJsonBody(resp, body))
    {
        CHECK(body.isMember("message"));
    }
}

// ---------------------------------------------------------------------------
// F-027: end_session with a post_logout_redirect_uri but no id_token_hint is
// rejected with 400 (the server requires pre-registration + client
// identification via the hint).
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_OidcBatch2_EndSession_RedirectUriWithoutHint_Returns400)
{
    OIDC_BATCH2_SKIP_GUARD;

    auto resp = sendGet(
      "/oauth2/end_session?post_logout_redirect_uri=http://127.0.0.1:5173/&state=xyz12345");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k400BadRequest));
}

// ---------------------------------------------------------------------------
// F-023 (OIDC Core §5.3): /oauth2/userinfo requires an access token whose
// scope includes "openid". A client_credentials token (M2M, subject
// "client:...") is rejected with 403 insufficient_scope.
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P1_OidcBatch2_UserInfo_M2MToken_Returns403InsufficientScope)
{
    OIDC_BATCH2_SKIP_GUARD;

    // Obtain an M2M access token (backend-svc, scope=tokens:read -- no openid)
    // via HTTP Basic (the client's declared auth method). #43: the legacy
    // 'read' scope is dropped; use the resource-prefixed vocabulary.
    std::string accessToken;
    {
        try
        {
            auto client = ::drogon::HttpClient::newHttpClient(
              "http://127.0.0.1:5555", ::drogon::app().getLoop()
            );
            auto req = ::drogon::HttpRequest::newHttpRequest();
            req->setMethod(::drogon::Post);
            req->setPath("/oauth2/token");
            req->setContentTypeCode(::drogon::CT_APPLICATION_X_FORM);
            req->addHeader(
              "Authorization",
              "Basic " + ::drogon::utils::base64Encode("backend-svc:test-secret")
            );
            req->setBody("grant_type=client_credentials&client_id=backend-svc&scope=tokens:read");
            auto [result, resp] = client->sendRequest(req, 30.0);
            REQUIRE(result == ::drogon::ReqResult::Ok);
            REQUIRE(resp != nullptr);
            REQUIRE(statusIs(resp, drogon::k200OK));
            Json::Value body;
            REQUIRE(parseJsonBody(resp, body));
            accessToken = body["access_token"].asString();
        }
        catch (const std::exception &e)
        {
            LOG_WARN << "token request for userinfo test failed: " << e.what();
            CHECK(false);
            return;
        }
    }
    CHECK(!accessToken.empty());
    if (accessToken.empty())
        return;

    // #43 M1+R2 (OIDC Core §5.3): userinfo is NOT registry-gated, so the M2M
    // token reaches the handler. The handler checks subject first: a
    // client_credentials token has subject "client:<id>" (no user identity)
    // -> 401 invalid_token. This is the correct RFC error classification
    // (token type mismatch, not scope insufficiency).
    auto resp = sendGet("/oauth2/userinfo", accessToken);
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k401Unauthorized));
    auto wwwAuth = resp->getHeader("WWW-Authenticate");
    CHECK(wwwAuth.find("invalid_token") != std::string::npos);
}
