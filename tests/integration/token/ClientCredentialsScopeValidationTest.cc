// tests/integration/token/ClientCredentialsScopeValidationTest.cc
//
// P0 #2 (评审问题点有效性分析报告, review finding 2 / RFC 6749 §3.3): the
// client_credentials grant used to echo the requested scope back unchecked
// (and hardcoded "read" when omitted), so any authenticated client could
// self-assign e.g. "admin". The handler now validates against the client's
// registered allowlist (oauth2_client_scopes):
//   - requested scope not covered by the allowlist -> 400 invalid_scope
//   - allowed requested scope -> granted verbatim
//   - omitted scope -> defaults to the full registered scope set
//
// Fixture: seed client `backend-svc` (CONFIDENTIAL, secret "test-secret",
// grant client_credentials -- apps/server/seed/dev_backend_client.sql). The
// scope grants (read/write) are ensured idempotently below so the test does
// not depend on the seed script having been re-run after the P0 #2 change.

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

HttpResponsePtr postTokenForm(const std::string &body)
{
    try
    {
        auto client = HttpClient::newHttpClient(kBaseUrl);
        auto req = HttpRequest::newHttpRequest();
        req->setMethod(Post);
        req->setPath("/oauth2/token");
        req->setContentTypeCode(CT_APPLICATION_X_FORM);
        // F-017: backend-svc is seeded with token_endpoint_auth_method=
        // client_secret_basic, so its secret MUST arrive via HTTP Basic
        // (sending it in the body is now rejected). Encode the same
        // backend-svc/test-secret pair as Basic auth here.
        req->addHeader(
          "Authorization",
          "Basic " + ::drogon::utils::base64Encode("backend-svc:test-secret")
        );
        req->setBody(body);
        auto [result, resp] = client->sendRequest(req, /*timeout=*/30.0);
        if (result != ReqResult::Ok || resp == nullptr)
            return nullptr;
        return resp;
    }
    catch (const std::exception &e)
    {
        LOG_WARN << "postTokenForm failed (server likely unreachable): " << e.what();
        return nullptr;
    }
}

bool serverReachable()
{
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        if (postTokenForm("grant_type=client_credentials") != nullptr)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return false;
}

// Idempotently grant read/write to backend-svc (mirrors
// dev_backend_client.sql) so this test is self-sufficient on databases
// seeded before the P0 #2 seed change. Returns false on SQL failure.
bool ensureBackendSvcScopes()
{
    auto db = app().getDbClient();
    if (!db)
        return false;
    std::promise<bool> p;
    db->execSqlAsync(
      "INSERT INTO oauth2_client_scopes (client_id, scope_name) "
      "SELECT 'backend-svc', name FROM oauth2_scopes WHERE name IN ('read', 'write') "
      "ON CONFLICT (client_id, scope_name) DO NOTHING",
      [&](const Result &) { p.set_value(true); },
      [&](const DrogonDbException &e) {
          LOG_ERROR << "ensureBackendSvcScopes: " << e.base().what();
          p.set_value(false);
      }
    );
    return p.get_future().get();
}

// F-017: backend-svc is seeded client_secret_basic, so the secret travels
// in the Authorization header (set in postTokenForm), not the body. Keep
// client_id in the body for clients that key off it; the secret is omitted
// here to satisfy the client_secret_basic enforcement.
constexpr const char *kCredentials = "client_id=backend-svc";
}  // namespace

DROGON_TEST(Integration_P0_ClientCredentials_ScopeValidation_RejectsUnregisteredScope)
{
    auto plugin = app().getPlugin<OAuth2Plugin>();
    if (!plugin || plugin->getStorageType() == "memory")
    {
        // backend-svc is Postgres seed data; memory mode has no
        // client_credentials fixture.
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

    // Negative: "admin" is a registered scope name but NOT granted to
    // backend-svc -> invalid_scope (400), no token issued.
    {
        auto resp = postTokenForm(
          std::string("grant_type=client_credentials&") + kCredentials + "&scope=admin"
        );
        REQUIRE(resp != nullptr);
        CHECK(resp->getStatusCode() == k400BadRequest);
        Json::Value body;
        REQUIRE(parseBody(resp, body));
        CHECK(body["error"].asString() == "invalid_scope");
        CHECK(!body.isMember("access_token"));
    }

    // Negative: a partially-exceeding list ("read admin") must also be
    // rejected outright, not silently narrowed.
    {
        auto resp = postTokenForm(
          std::string("grant_type=client_credentials&") + kCredentials + "&scope=read%20admin"
        );
        REQUIRE(resp != nullptr);
        CHECK(resp->getStatusCode() == k400BadRequest);
        Json::Value body;
        REQUIRE(parseBody(resp, body));
        CHECK(body["error"].asString() == "invalid_scope");
    }

    // Positive: a granted scope is issued verbatim.
    {
        auto resp = postTokenForm(
          std::string("grant_type=client_credentials&") + kCredentials + "&scope=read"
        );
        REQUIRE(resp != nullptr);
        CHECK(resp->getStatusCode() == k200OK);
        Json::Value body;
        REQUIRE(parseBody(resp, body));
        CHECK(body.isMember("access_token"));
        CHECK(body["scope"].asString() == "read");
    }

    // Omitted scope: defaults to the full registered set (read/write, order
    // not guaranteed by the join) -- not the pre-fix hardcoded "read".
    {
        auto resp = postTokenForm(std::string("grant_type=client_credentials&") + kCredentials);
        REQUIRE(resp != nullptr);
        CHECK(resp->getStatusCode() == k200OK);
        Json::Value body;
        REQUIRE(parseBody(resp, body));
        CHECK(body.isMember("access_token"));
        const std::string granted = body["scope"].asString();
        CHECK(granted.find("read") != std::string::npos);
        CHECK(granted.find("write") != std::string::npos);
    }
}
