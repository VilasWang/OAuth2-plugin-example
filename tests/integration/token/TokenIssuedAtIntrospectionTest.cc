// tests/integration/token/TokenIssuedAtIntrospectionTest.cc
//
// P2 #10 (评审问题点有效性分析报告, review finding 10 / RFC 7662 §2.2): every
// access-token issuance path used to leave `issuedAt` at the 0 default (except
// the device_code and GitHub flows, which already set it). The introspection
// endpoint silently omits `iat` when it is 0, so introspected tokens had no
// issue timestamp and admin token listings sorted wrong. The three remaining
// issuance paths (authorization_code, refresh_token, client_credentials) now
// set issuedAt = now.
//
// This test exercises the client_credentials path end-to-end: issue a token,
// introspect it, and assert `iat` is present and within a small window of the
// issue moment (proves issuedAt is a real timestamp, not the 0 default).
//
// Fixture: CONFIDENTIAL client `backend-svc` (secret "test-secret") with the
// read scope ensured idempotently below (same pattern as
// ClientCredentialsScopeValidationTest).

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <json/json.h>

#include <chrono>
#include <future>
#include <string>
#include <thread>

using namespace drogon;
using namespace drogon::orm;

namespace
{
constexpr const char *kBaseUrl = "http://127.0.0.1:5555";

bool parseBody(const HttpResponsePtr &resp, Json::Value &out)
{
    const std::string body(resp->getBody());
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    return reader->parse(body.data(), body.data() + body.size(), &out, &errs);
}

HttpResponsePtr postForm(const std::string &path, const std::string &body)
{
    try
    {
        auto client = HttpClient::newHttpClient(kBaseUrl);
        auto req = HttpRequest::newHttpRequest();
        req->setMethod(Post);
        req->setPath(path);
        req->setContentTypeCode(CT_APPLICATION_X_FORM);
        req->setBody(body);
        auto [result, resp] = client->sendRequest(req, /*timeout=*/30.0);
        if (result != ReqResult::Ok || resp == nullptr)
            return nullptr;
        return resp;
    }
    catch (const std::exception &e)
    {
        LOG_WARN << "postForm failed (server likely unreachable): " << e.what();
        return nullptr;
    }
}

// Introspect (RFC 7662) authenticates the CALLING CLIENT via HTTP Basic or
// form-posted client_id/client_secret -- it is NOT gated by OAuth2AuthFilter
// (that filter demands a resource-owner Bearer token, which is the wrong auth
// model for a protocol endpoint; it was removed in the introspect/revoke
// RFC-compliance fix). extractClientCredentials reads client_id/client_secret
// from the POST body when the Authorization header is not Basic, so we pass
// the token-to-introspect + the client creds in the body. (The helper still
// accepts a bearerToken param for historical compatibility with the previous
// Bearer+body-creds model, but that header is now ignored by the route.)
HttpResponsePtr introspectWithBearer(
  const std::string &bearerToken,
  const std::string &introspectedToken,
  const std::string &clientCreds
)
{
    try
    {
        auto client = HttpClient::newHttpClient(kBaseUrl);
        auto req = HttpRequest::newHttpRequest();
        req->setMethod(Post);
        req->setPath("/oauth2/introspect");
        req->setContentTypeCode(CT_APPLICATION_X_FORM);
        req->addHeader("Authorization", "Bearer " + bearerToken);
        req->setBody(std::string("token=") + introspectedToken + "&" + clientCreds);
        auto [result, resp] = client->sendRequest(req, /*timeout=*/30.0);
        if (result != ReqResult::Ok || resp == nullptr)
            return nullptr;
        return resp;
    }
    catch (const std::exception &e)
    {
        LOG_WARN << "introspectWithBearer failed (server likely unreachable): " << e.what();
        return nullptr;
    }
}

bool serverReachable()
{
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        if (postForm("/oauth2/token", "grant_type=client_credentials") != nullptr)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return false;
}

bool ensureBackendSvcScopes()
{
    auto db = app().getDbClient();
    if (!db)
        return false;
    std::promise<bool> p;
    db->execSqlAsync(
      "INSERT INTO oauth2_client_scopes (client_id, scope_name) "
      "SELECT 'backend-svc', name FROM oauth2_scopes WHERE name = 'read' "
      "ON CONFLICT (client_id, scope_name) DO NOTHING",
      [&](const Result &) { p.set_value(true); },
      [&](const DrogonDbException &e) {
          LOG_ERROR << "ensureBackendSvcScopes: " << e.base().what();
          p.set_value(false);
      }
    );
    return p.get_future().get();
}

constexpr const char *kCredentials = "client_id=backend-svc&client_secret=test-secret";

int64_t nowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch()
    )
      .count();
}
}  // namespace

DROGON_TEST(Integration_P2_TokenIssuedAt_ClientCredentials_IntrospectionHasIat)
{
    auto plugin = app().getPlugin<OAuth2Plugin>();
    if (!plugin || plugin->getStorageType() == "memory")
    {
        // backend-svc is Postgres seed data; memory mode has no fixture.
        CHECK(true);
        return;
    }
    if (!serverReachable())
    {
        LOG_INFO << "Skipping: HTTP listener not reachable on " << kBaseUrl;
        CHECK(true);
        return;
    }
    REQUIRE(ensureBackendSvcScopes());

    // 1. Issue a client_credentials token.
    const int64_t before = nowSeconds();
    auto issueResp =
      postForm("/oauth2/token", std::string("grant_type=client_credentials&") + kCredentials);
    REQUIRE(issueResp != nullptr);
    CHECK(issueResp->getStatusCode() == k200OK);
    Json::Value issueBody;
    REQUIRE(parseBody(issueResp, issueBody));
    REQUIRE(issueBody.isMember("access_token"));
    const std::string accessToken = issueBody["access_token"].asString();
    const int64_t after = nowSeconds();

    // 2. Introspect it. The same token serves as the Bearer credential that
    //    passes the filter; client creds go in the body.
    auto introResp = introspectWithBearer(accessToken, accessToken, kCredentials);
    REQUIRE(introResp != nullptr);
    CHECK(introResp->getStatusCode() == k200OK);
    Json::Value introBody;
    REQUIRE(parseBody(introResp, introBody));
    CHECK(introBody["active"].asBool() == true);

    // 3. iat MUST be present (not silently omitted as when issuedAt == 0) and
    //    be a real, recent timestamp. The window is loose because the token's
    //    persisted issued_at is stamped at async-save time, which can land a
    //    moment after `after`; the point is to prove iat is non-zero and
    //    current, not to pin the exact second.
    REQUIRE(introBody.isMember("iat"));
    const int64_t iat = introBody["iat"].asInt64();
    CHECK(iat > 0);
    CHECK(iat >= before - 5);
    CHECK(iat <= after + 60);
}
