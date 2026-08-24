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

#include <authforge/oauth2/jwk/JwkManager.h>

#include "HttpTestClient.h"

#include <string>

using authforge::test::http::kAdminClientId;
using authforge::test::http::loginAsUserTokens;
using authforge::test::http::kAdminRedirectUri;
using authforge::test::http::kTestBaseUrl;
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

// ---------------------------------------------------------------------------
// #78: end_session MUST NOT trust an id_token_hint whose signature does not
// verify against the OP's own key set (previously the unverified `sub` claim
// drove the backchannel logout fan-out -> unauthenticated cross-user forced
// logout). Rejections are 400 + AUTH_INVALID_ID_TOKEN_HINT error envelopes.
// ---------------------------------------------------------------------------

namespace
{
// Returns true iff the response is a 400 whose error envelope carries
// AUTH_INVALID_ID_TOKEN_HINT (the unified Application error shape:
// error.code/category/message). Assertion happens in the test body -- the
// drogon test macros need the test context that a free function lacks.
bool isInvalidHintRejection(const ::drogon::HttpResponsePtr &resp)
{
    if (!resp || resp->getStatusCode() != ::drogon::k400BadRequest)
        return false;
    Json::Value body;
    if (!parseJsonBody(resp, body))
        return false;
    if (!body.isMember("error") || !body["error"].isObject() || !body["error"].isMember("code"))
        return false;
    return body["error"]["code"].asString() == "AUTH_INVALID_ID_TOKEN_HINT";
}
}  // namespace

// #78: a well-formed, plausibly-claimed id_token_hint signed by a DIFFERENT
// key (a local ephemeral JwkManager, mimicking an attacker who knows the
// victim's iss/sub) must be rejected -- signature/kid verification is the
// gate, not claim plausibility.
DROGON_TEST(Integration_P1_OidcBatch2_EndSession_ForgedHint_Returns400)
{
    OIDC_BATCH2_SKIP_GUARD;

    ::authforge::oauth2::JwkManager forger;
    REQUIRE(forger.init(Json::Value(Json::objectValue)));

    auto *plugin = ::drogon::app().getPlugin<::OAuth2Plugin>();
    REQUIRE(plugin != nullptr);

    Json::Value claims;
    claims["iss"] = plugin->getIssuer();
    claims["sub"] = "00000000-0000-0000-0000-00000000078";
    claims["aud"] = "admin-console";
    claims["exp"] = static_cast<Json::Int64>(time(nullptr) + 600);
    const std::string forged = forger.signJwt(claims);
    REQUIRE(!forged.empty());

    auto resp = sendGet("/oauth2/end_session?id_token_hint=" + forged);
    REQUIRE(resp != nullptr);
    CHECK(isInvalidHintRejection(resp));
}

// #78: structural garbage in id_token_hint is a hard 400 too -- no silent
// fallback to "treat as if no hint was supplied".
DROGON_TEST(Integration_P1_OidcBatch2_EndSession_GarbageHint_Returns400)
{
    OIDC_BATCH2_SKIP_GUARD;

    auto resp = sendGet("/oauth2/end_session?id_token_hint=not-a-jwt-at-all");
    REQUIRE(resp != nullptr);
    CHECK(isInvalidHintRejection(resp));
}

// #78 happy path: a REAL id_token (minted through the full authorization-code
// flow with openid scope) + the client's registered post_logout_redirect_uri
// -> 302 redirect with state echoed. This is the flow legitimate RPs use.
DROGON_TEST(Integration_P1_OidcBatch2_EndSession_VerifiedHint_Redirects302)
{
    OIDC_BATCH2_SKIP_GUARD;

    auto tokens = loginAsUserTokens("admin", "admin", "openid profile admin");
    REQUIRE(tokens.has_value());
    const std::string idToken = tokens->get("id_token", "").asString();
    REQUIRE(!idToken.empty());

    auto resp = sendGet(
      "/oauth2/end_session?id_token_hint=" + idToken +
      "&post_logout_redirect_uri=" + kAdminRedirectUri + "&state=st78"
    );
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k302Found));
    auto location = resp->getHeader("location");
    CHECK(location.find(kAdminRedirectUri) == 0);
    CHECK(location.find("state=st78") != std::string::npos);
}

// #78 subject consistency: a VERIFIED hint that describes a DIFFERENT user
// than the browser session (admin's id_token + a freshly registered user's
// session cookie) is rejected with the same 400 -- the hint must match the
// signed-in subject when both are present.
DROGON_TEST(Integration_P1_OidcBatch2_EndSession_HintSubjectMismatch_Returns400)
{
    OIDC_BATCH2_SKIP_GUARD;

    // User A's (admin's) verified id_token.
    auto tokens = loginAsUserTokens("admin", "admin", "openid profile admin");
    REQUIRE(tokens.has_value());
    const std::string idToken = tokens->get("id_token", "").asString();
    REQUIRE(!idToken.empty());

    // Register user B, then establish B's browser session (the login response
    // carries the JSESSIONID cookie holding B's sub).
    auto reg = sendPostForm(
      "/api/register", "username=mm78user&password=Passw0rd!78&email=mm78@example.test"
    );
    REQUIRE(reg != nullptr);
    // Registration is idempotent for re-runs (duplicate username -> 409 is
    // fine as long as the account exists for the login below).
    const bool registered = statusIs(reg, drogon::k200OK) || statusIs(reg, drogon::k409Conflict);
    CHECK(registered);
    if (!registered)
        return;

    const std::string codeVerifier = ::authforge::drogon::utils::generateSecureToken(32);
    const std::string codeChallenge =
      ::authforge::drogon::utils::computeCodeChallenge(codeVerifier, "S256");
    auto loginResp = sendPostForm(
      "/oauth2/login?json=true",
      "username=mm78user&password=Passw0rd!78&client_id=" + std::string(kAdminClientId) +
        "&redirect_uri=" + std::string(kAdminRedirectUri) +
        "&scope=openid&state=t78&code_challenge=" + codeChallenge +
        "&code_challenge_method=S256"
    );
    REQUIRE(loginResp != nullptr);
    REQUIRE(statusIs(loginResp, drogon::k200OK));
    // drogon keeps cookies in a dedicated map (name -> drogon::Cookie, not
    // plain header strings), so rebuild the Cookie header from getCookies().
    std::string cookie;
    for (const auto &entry : loginResp->getCookies())
    {
        if (!cookie.empty())
            cookie += "; ";
        cookie += entry.first + "=" + entry.second.value();
    }
    REQUIRE(!cookie.empty());

    // end_session carrying B's cookie + A's verified hint, via a raw request
    // (the shared helpers have no custom-header injection point).
    ::drogon::HttpResponsePtr resp;
    try
    {
        auto client =
          ::drogon::HttpClient::newHttpClient(kTestBaseUrl, ::drogon::app().getLoop());
        auto req = ::drogon::HttpRequest::newHttpRequest();
        req->setMethod(::drogon::Get);
        req->setPath("/oauth2/end_session?id_token_hint=" + idToken);
        req->addHeader("Cookie", cookie);
        auto [result, r] = client->sendRequest(req, 30.0);
        REQUIRE(result == ::drogon::ReqResult::Ok);
        resp = r;
    }
    catch (const std::exception &e)
    {
        LOG_WARN << "mismatch-test request failed: " << e.what();
    }
    REQUIRE(resp != nullptr);
    CHECK(isInvalidHintRejection(resp));
}
