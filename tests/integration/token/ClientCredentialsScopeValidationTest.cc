// tests/integration/token/ClientCredentialsScopeValidationTest.cc
//
// P0 #2 / #43 (RFC 6749 §3.3): the client_credentials grant validates the
// requested scope against the client's registered allowlist
// (oauth2_client_scopes):
//   - requested scope not covered by the allowlist -> 400 invalid_scope
//   - allowed requested scope -> granted verbatim
//   - omitted scope -> defaults to the full registered scope set
//
// #43: the legacy bare 'read'/'write' scopes are dropped; the test now uses
// the resource-prefixed vocabulary (tokens:read, tokens:write, ...).
//
// Fixture: seed client `backend-svc` (CONFIDENTIAL, secret "test-secret",
// grant client_credentials -- apps/server/seed/dev_backend_client.sql). The
// scope grants are ensured idempotently below so the test does not depend on
// the seed script having been re-run.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <json/json.h>

#include <chrono>
#include <future>
#include <set>
#include <sstream>
#include <string>
#include <thread>

using namespace drogon;
using namespace drogon::orm;

namespace
{
constexpr const char *kBaseUrl = "http://127.0.0.1:5555";

// The resource scopes granted to backend-svc (mirrors dev_backend_client.sql
// post-#43). Must stay in sync with the seed file.
const std::set<std::string> &backendSvcScopes()
{
    static const std::set<std::string> s = {"tokens:read", "tokens:write", "clients:read", "users:read"};
    return s;
}

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
        // client_secret_basic, so its secret MUST arrive via HTTP Basic.
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

// Idempotently grant the resource scopes to backend-svc (mirrors
// dev_backend_client.sql post-#43). Returns false on SQL failure.
// Deletes ALL existing backend-svc scopes first so the test is immune to
// scope drift from other tests that may run before this one and add scopes
// (e.g. admin client-scope CRUD tests adding 'read' from the legacy V006
// seed). Without the clean slate, the exact-set CHECK below would fail.
bool ensureBackendSvcScopes()
{
    auto db = app().getDbClient();
    if (!db)
        return false;
    std::promise<bool> p;
    auto db2 = app().getDbClient();
    // Step 1: clean slate -- remove all existing backend-svc scope grants.
    db->execSqlAsync(
      "DELETE FROM oauth2_client_scopes WHERE client_id = 'backend-svc'",
      [db2, &p](const Result &) {
          // Step 2: insert exactly the expected scope set.
          db2->execSqlAsync(
            "INSERT INTO oauth2_client_scopes (client_id, scope_name) "
            "SELECT 'backend-svc', name FROM oauth2_scopes "
            "WHERE name IN ('tokens:read', 'tokens:write', 'clients:read', 'users:read') "
            "ON CONFLICT (client_id, scope_name) DO NOTHING",
            [&](const Result &) { p.set_value(true); },
            [&](const DrogonDbException &e) {
                LOG_ERROR << "ensureBackendSvcScopes insert: " << e.base().what();
                p.set_value(false);
            }
          );
      },
      [&](const DrogonDbException &e) {
          LOG_ERROR << "ensureBackendSvcScopes delete: " << e.base().what();
          p.set_value(false);
      }
    );
    return p.get_future().get();
}

// Split a space-delimited scope string into a set for exact-membership checks
// (avoids the substring trap: find("read") would match "tokens:read").
std::set<std::string> scopeSet(const std::string &spaceDelimited)
{
    std::set<std::string> result;
    std::istringstream iss(spaceDelimited);
    std::string token;
    while (iss >> token)
        result.insert(token);
    return result;
}

// F-017: backend-svc is seeded client_secret_basic, so the secret travels in
// the Authorization header (set in postTokenForm), not the body.
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

    // Negative: a partially-exceeding list ("tokens:read admin") must also be
    // rejected outright, not silently narrowed.
    {
        auto resp = postTokenForm(
          std::string("grant_type=client_credentials&") + kCredentials + "&scope=tokens:read%20admin"
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
          std::string("grant_type=client_credentials&") + kCredentials + "&scope=tokens:read"
        );
        REQUIRE(resp != nullptr);
        CHECK(resp->getStatusCode() == k200OK);
        Json::Value body;
        REQUIRE(parseBody(resp, body));
        CHECK(body.isMember("access_token"));
        CHECK(body["scope"].asString() == "tokens:read");
    }

    // Omitted scope: defaults to the full registered set (order not guaranteed
    // by the join).
    {
        auto resp = postTokenForm(std::string("grant_type=client_credentials&") + kCredentials);
        REQUIRE(resp != nullptr);
        CHECK(resp->getStatusCode() == k200OK);
        Json::Value body;
        REQUIRE(parseBody(resp, body));
        CHECK(body.isMember("access_token"));
        // Exact set match (not substring) to avoid the tokens:read/read trap.
        CHECK(scopeSet(body["scope"].asString()) == backendSvcScopes());
    }
}
