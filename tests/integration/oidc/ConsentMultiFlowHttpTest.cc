// tests/integration/oidc/ConsentMultiFlowHttpTest.cc
//
// #144: (a) the consent CSRF nonce lives in a bounded multi-slot list, so two
// concurrent authorize flows on one session no longer overwrite each other's
// slot (the single-slot regression made the first consent page permanently
// un-submittable); (b) a session paused at the MFA challenge (amr="pwd",
// mfa_pending marker) must NOT pass the consent/authorize gates as a fully
// authenticated session.
#include <drogon/drogon_test.h>
#include <drogon/HttpClient.h>
#include <fulla/identity/TotpUtils.h>
#include <fulla/drogon/adapters/OpenSslCryptoProvider.h>
#include <fulla/drogon/utils/PasswordHasher.h>
#include <json/json.h>

#include <future>
#include <string>

#include "../../common/HttpTestClient.h"

using namespace drogon;
using namespace drogon::orm;

namespace
{

HttpResponsePtr post(const std::string &path, const std::string &body, const std::string &cookie = "")
{
    auto client = HttpClient::newHttpClient("http://127.0.0.1:5555", app().getLoop());
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath(path);
    req->setBody(body);
    req->setContentTypeCode(::drogon::CT_APPLICATION_X_FORM);
    if (!cookie.empty())
        req->addHeader("Cookie", cookie);
    auto [result, resp] = client->sendRequest(req, 30.0);
    if (result != ReqResult::Ok)
        return nullptr;
    return resp;
}

std::string loginCookie(const std::string &username, const std::string &password)
{
    auto resp = fulla::test::http::sendPostForm(
      "/oauth2/login?json=true",
      "username=" + username + "&password=" + password +
        "&client_id=admin-console&redirect_uri=http%3A%2F%2F127.0.0.1%3A5174%2Fadmin%2Fcallback"
        "&scope=openid&state=g1&code_challenge=F_TTxId01kOTYIcFSCqZnz9wQ-6F1aJ1vtm1YoBy8po&code_challenge_method=S256"
    );
    if (!resp || resp->getStatusCode() != k200OK)
        return "";
    std::string cookie;
    for (const auto &entry : resp->getCookies())
    {
        if (!cookie.empty())
            cookie += "; ";
        cookie += entry.first + "=" + entry.second.value();
    }
    return cookie;
}

// authorize (prompt=consent) with a per-flow state; extracts the minted
// consent_csrf and user_id. Returns false on any failure.
bool authorizeMintNonce(
  const std::string &cookieIn,
  const std::string &state,
  std::string &csrf,
  std::string &userId,
  std::string &cookieOut
)
{
    cookieOut = cookieIn;
    auto client = HttpClient::newHttpClient("http://127.0.0.1:5555", app().getLoop());
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Get);
    req->setPath("/oauth2/authorize");
    req->setParameter("response_type", "code");
    req->setParameter("client_id", "vue-client");
    req->setParameter("redirect_uri", "http://127.0.0.1:5173/callback");
    req->setParameter("scope", "openid");
    req->setParameter("state", state);
    req->setParameter("prompt", "consent");
    req->setParameter("code_challenge", "F_TTxId01kOTYIcFSCqZnz9wQ-6F1aJ1vtm1YoBy8po");
    req->setParameter("code_challenge_method", "plain");
    req->addHeader("Cookie", cookieOut);
    auto [result, resp] = client->sendRequest(req, 30.0);
    if (result != ReqResult::Ok || resp->getStatusCode() != k302Found)
        return false;
    for (const auto &entry : resp->getCookies())
    {
        std::string name = entry.first + "=";
        if (cookieOut.find(name) == std::string::npos)
            cookieOut += "; " + name + entry.second.value();
    }
    auto location = resp->getHeader("Location");
    auto csrfPos = location.find("consent_csrf=");
    auto uidPos = location.find("user_id=");
    if (csrfPos == std::string::npos || uidPos == std::string::npos)
        return false;
    auto csrfEnd = location.find('&', csrfPos);
    csrf = location.substr(csrfPos + 13,
                           (csrfEnd == std::string::npos ? location.size() : csrfEnd) - csrfPos - 13);
    auto uidEnd = location.find('&', uidPos);
    userId = location.substr(uidPos + 8,
                             (uidEnd == std::string::npos ? location.size() : uidEnd) - uidPos - 8);
    return !csrf.empty() && !userId.empty();
}

HttpResponsePtr authorize(const std::string &cookie, const std::string &extraParam = "")
{
    auto client = HttpClient::newHttpClient("http://127.0.0.1:5555", app().getLoop());
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Get);
    req->setPath("/oauth2/authorize");
    req->setParameter("response_type", "code");
    req->setParameter("client_id", "vue-client");
    req->setParameter("redirect_uri", "http://127.0.0.1:5173/callback");
    req->setParameter("scope", "openid");
    req->setParameter("state", "silentstate01");
    req->setParameter("code_challenge", "F_TTxId01kOTYIcFSCqZnz9wQ-6F1aJ1vtm1YoBy8po");
    req->setParameter("code_challenge_method", "plain");
    if (!extraParam.empty())
        req->setParameter("prompt", extraParam);
    req->addHeader("Cookie", cookie);
    auto [result, resp] = client->sendRequest(req, 30.0);
    if (result != ReqResult::Ok)
        return nullptr;
    return resp;
}

// Sync SQL helper (future/promise pattern used by the MfaCrossClient tests).
bool execSql(const std::string &sql)
{
    auto db = app().getDbClient();
    if (!db)
        return false;
    std::promise<bool> p;
    db->execSqlAsync(
      sql,
      [&](const Result &) { p.set_value(true); },
      [&](const DrogonDbException &) { p.set_value(false); }
    );
    return p.get_future().get();
}

// Internal id of the seeded admin (Gate 2 needs user_id = session["userId"]).
std::string adminInternalId()
{
    auto db = app().getDbClient();
    if (!db)
        return "";
    std::promise<std::string> p;
    db->execSqlAsync(
      "SELECT id FROM users WHERE username = 'admin'",
      [&](const Result &rows) {
          p.set_value(rows.empty() ? "" : std::to_string(rows[0]["id"].as<int32_t>()));
      },
      [&](const DrogonDbException &) { p.set_value(""); }
    );
    return p.get_future().get();
}

}  // namespace

// Two concurrent authorize flows on one session (same client, two tabs ->
// two states): BOTH minted nonces must remain submittable. The single-slot
// implementation made the first flow's consent page fail with a hard 400.
DROGON_TEST(Integration_P1_ConsentMultiFlow_TwoConcurrentNonces_BothApprove)
{
    if (!fulla::test::http::postgresAvailable())
    {
        CHECK(true);
        return;
    }
    auto cookie = loginCookie("admin", "admin");
    REQUIRE(!cookie.empty());

    std::string csrf1, csrf2, userId, cookieA, cookieB;
    REQUIRE(authorizeMintNonce(cookie, "multiflowstate1", csrf1, userId, cookieA));
    REQUIRE(authorizeMintNonce(cookie, "multiflowstate2", csrf2, userId, cookieB));
    REQUIRE(!csrf1.empty());
    REQUIRE(csrf1 != csrf2);

    // First flow's consent still validates after the second mint.
    auto approve1 = post(
      "/oauth2/consent",
      "client_id=vue-client&user_id=" + userId +
        "&scope=openid&redirect_uri=http%3A%2F%2F127.0.0.1%3A5173%2Fcallback&state=multiflowstate1&consent_csrf=" +
        csrf1 + "&action=approve",
      cookieA
    );
    REQUIRE(approve1 != nullptr);
    CHECK(approve1->getStatusCode() == k302Found);
    CHECK(approve1->getHeader("Location").find("code=") != std::string::npos);

    // Second flow's consent validates too (its slot was not overwritten).
    auto approve2 = post(
      "/oauth2/consent",
      "client_id=vue-client&user_id=" + userId +
        "&scope=openid&redirect_uri=http%3A%2F%2F127.0.0.1%3A5173%2Fcallback&state=multiflowstate2&consent_csrf=" +
        csrf2 + "&action=approve",
      cookieB
    );
    REQUIRE(approve2 != nullptr);
    CHECK(approve2->getStatusCode() == k302Found);
    CHECK(approve2->getHeader("Location").find("code=") != std::string::npos);
}

// The slot list is bounded (kMaxSlots = 5): the 6th concurrent mint evicts
// the oldest entry, so THAT consent fails with the standard expired/mismatch
// 400 (not a silent success and not a 500).
DROGON_TEST(Integration_P1_ConsentMultiFlow_NonceCapEvictsOldest)
{
    if (!fulla::test::http::postgresAvailable())
    {
        CHECK(true);
        return;
    }
    auto cookie = loginCookie("admin", "admin");
    REQUIRE(!cookie.empty());

    std::string csrfOldest, csrfNewest, userId, cookieOldest, cookieNewest;
    REQUIRE(authorizeMintNonce(cookie, "capstate01", csrfOldest, userId, cookieOldest));
    for (int i = 2; i <= 5; ++i)
    {
        std::string csrf, uid, cookieTmp;
        REQUIRE(authorizeMintNonce(cookie, "capstate0" + std::to_string(i), csrf, uid, cookieTmp));
    }
    REQUIRE(authorizeMintNonce(cookie, "capstate06", csrfNewest, userId, cookieNewest));

    // The 6th mint evicted the oldest -> its nonce is no longer consumable.
    auto evicted = post(
      "/oauth2/consent",
      "client_id=vue-client&user_id=" + userId +
        "&scope=openid&redirect_uri=http%3A%2F%2F127.0.0.1%3A5173%2Fcallback&state=capstate01&consent_csrf=" +
        csrfOldest + "&action=approve",
      cookieOldest
    );
    REQUIRE(evicted != nullptr);
    CHECK(evicted->getStatusCode() == k400BadRequest);

    // The newest nonce is still live.
    auto live = post(
      "/oauth2/consent",
      "client_id=vue-client&user_id=" + userId +
        "&scope=openid&redirect_uri=http%3A%2F%2F127.0.0.1%3A5173%2Fcallback&state=capstate06&consent_csrf=" +
        csrfNewest + "&action=approve",
      cookieNewest
    );
    REQUIRE(live != nullptr);
    CHECK(live->getStatusCode() == k302Found);
}

// A session paused at the MFA challenge (amr="pwd", mfa_pending marker) must
// be refused by the consent gate with 401 AUTH_MFA_REQUIRED — passing Gate 1
// (login writes userId/sub before the MFA decision) but granting consent
// would mint a code for an MFA-required account without the second factor.
DROGON_TEST(Integration_P1_MfaPending_Consent_Returns401)
{
    if (!fulla::test::http::postgresAvailable())
    {
        CHECK(true);
        return;
    }
    REQUIRE(execSql("UPDATE users SET mfa_enabled = true, mfa_secret = 'JBSWY3DPEHPK3PXP' WHERE username = 'admin'"));

    // First factor only -> mfa_required response, session is MFA-pending.
    auto resp = fulla::test::http::sendPostForm(
      "/oauth2/login?json=true",
      "username=admin&password=admin"
      "&client_id=admin-console&redirect_uri=http%3A%2F%2F127.0.0.1%3A5174%2Fadmin%2Fcallback"
      "&scope=openid&state=g1&code_challenge=F_TTxId01kOTYIcFSCqZnz9wQ-6F1aJ1vtm1YoBy8po&code_challenge_method=S256"
    );
    REQUIRE(resp != nullptr);
    REQUIRE(resp->getStatusCode() == k200OK);
    Json::Value body;
    REQUIRE(fulla::test::http::parseJsonBody(resp, body));
    CHECK(body.get("mfa_required", false).asBool() == true);

    std::string cookie;
    for (const auto &entry : resp->getCookies())
    {
        if (!cookie.empty())
            cookie += "; ";
        cookie += entry.first + "=" + entry.second.value();
    }
    REQUIRE(!cookie.empty());

    // Restore the seed state BEFORE asserting: the mfa_pending SESSION marker
    // intentionally survives until the next login, and the DB flag must not
    // leak into other tests.
    REQUIRE(execSql("UPDATE users SET mfa_enabled = false, mfa_secret = NULL WHERE username = 'admin'"));

    const std::string userId = adminInternalId();
    REQUIRE(!userId.empty());

    auto consent = post(
      "/oauth2/consent",
      "client_id=vue-client&user_id=" + userId +
        "&scope=openid&redirect_uri=http%3A%2F%2F127.0.0.1%3A5173%2Fcallback&state=g1&consent_csrf=anything&action=approve",
      cookie
    );
    REQUIRE(consent != nullptr);
    CHECK(consent->getStatusCode() == k401Unauthorized);
    Json::Value errBody;
    REQUIRE(fulla::test::http::parseJsonBody(consent, errBody));
    CHECK(errBody["error"]["code"].asString() == "AUTH_MFA_REQUIRED");
}

// The authorize endpoint must treat an MFA-pending session as NOT logged in:
// silent authorize redirects to the login screen, and prompt=none answers
// with error=login_required (OIDC Core §3.1.2.1: no UI may be shown).
DROGON_TEST(Integration_P1_MfaPending_Authorize_BlockedAndPromptNone_LoginRequired)
{
    if (!fulla::test::http::postgresAvailable())
    {
        CHECK(true);
        return;
    }
    REQUIRE(execSql("UPDATE users SET mfa_enabled = true, mfa_secret = 'JBSWY3DPEHPK3PXP' WHERE username = 'admin'"));

    auto resp = fulla::test::http::sendPostForm(
      "/oauth2/login?json=true",
      "username=admin&password=admin"
      "&client_id=admin-console&redirect_uri=http%3A%2F%2F127.0.0.1%3A5174%2Fadmin%2Fcallback"
      "&scope=openid&state=g1&code_challenge=F_TTxId01kOTYIcFSCqZnz9wQ-6F1aJ1vtm1YoBy8po&code_challenge_method=S256"
    );
    REQUIRE(resp != nullptr);
    REQUIRE(resp->getStatusCode() == k200OK);

    std::string cookie;
    for (const auto &entry : resp->getCookies())
    {
        if (!cookie.empty())
            cookie += "; ";
        cookie += entry.first + "=" + entry.second.value();
    }
    REQUIRE(!cookie.empty());

    REQUIRE(execSql("UPDATE users SET mfa_enabled = false, mfa_secret = NULL WHERE username = 'admin'"));

    // Silent authorize: the MFA-pending session must NOT receive a code —
    // it is routed to the login screen like an anonymous session.
    auto silent = authorize(cookie);
    REQUIRE(silent != nullptr);
    CHECK(silent->getStatusCode() == k302Found);
    CHECK(silent->getHeader("Location").find("/login") != std::string::npos);
    CHECK(silent->getHeader("Location").find("code=") == std::string::npos);

    // prompt=none: error redirect to the client with login_required, never UI.
    auto promptNone = authorize(cookie, "none");
    REQUIRE(promptNone != nullptr);
    CHECK(promptNone->getStatusCode() == k302Found);
    auto location = promptNone->getHeader("Location");
    CHECK(location.find("http://127.0.0.1:5173/callback") != std::string::npos);
    CHECK(location.find("error=login_required") != std::string::npos);
}

// A FAILED TOTP verification must not leave the session MFA-elevated: the
// amr/auth_time session writes happen only after the code actually verifies
// (#144 companion fix — the pre-fix code wrote amr="pwd mfa" before
// verification). Combined with the mfa_pending gate the failed-verify session
// is still refused at authorize.
DROGON_TEST(Integration_P1_MfaWrongCode_SessionNotElevated)
{
    if (!fulla::test::http::postgresAvailable())
    {
        CHECK(true);
        return;
    }
    REQUIRE(execSql("UPDATE users SET mfa_enabled = true, mfa_secret = 'JBSWY3DPEHPK3PXP' WHERE username = 'admin'"));

    auto resp = fulla::test::http::sendPostForm(
      "/oauth2/login?json=true",
      "username=admin&password=admin"
      "&client_id=admin-console&redirect_uri=http%3A%2F%2F127.0.0.1%3A5174%2Fadmin%2Fcallback"
      "&scope=openid&state=g1&code_challenge=F_TTxId01kOTYIcFSCqZnz9wQ-6F1aJ1vtm1YoBy8po&code_challenge_method=S256"
    );
    REQUIRE(resp != nullptr);
    REQUIRE(resp->getStatusCode() == k200OK);
    Json::Value body;
    REQUIRE(fulla::test::http::parseJsonBody(resp, body));
    const std::string mfaToken = body.get("mfa_token", "").asString();
    REQUIRE(!mfaToken.empty());

    std::string cookie;
    for (const auto &entry : resp->getCookies())
    {
        if (!cookie.empty())
            cookie += "; ";
        cookie += entry.first + "=" + entry.second.value();
    }
    REQUIRE(!cookie.empty());

    // Wrong TOTP code -> 401; the session must not become MFA-elevated.
    auto wrong = post(
      "/oauth2/mfa/verify",
      "mfa_token=" + mfaToken + "&code=000000"
      "&client_id=admin-console&redirect_uri=http%3A%2F%2F127.0.0.1%3A5174%2Fadmin%2Fcallback"
      "&scope=openid",
      cookie
    );
    REQUIRE(wrong != nullptr);
    CHECK(wrong->getStatusCode() == k401Unauthorized);

    REQUIRE(execSql("UPDATE users SET mfa_enabled = false, mfa_secret = NULL WHERE username = 'admin'"));

    // The session stays MFA-pending (never elevated) -> authorize is refused.
    auto silent = authorize(cookie);
    REQUIRE(silent != nullptr);
    CHECK(silent->getStatusCode() == k302Found);
    CHECK(silent->getHeader("Location").find("/login") != std::string::npos);
    CHECK(silent->getHeader("Location").find("code=") == std::string::npos);
}
