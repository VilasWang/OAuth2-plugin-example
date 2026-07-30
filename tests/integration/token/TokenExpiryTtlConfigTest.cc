// tests/integration/token/TokenExpiryTtlConfigTest.cc
//
// P1 #6 (评审问题点有效性分析报告, review finding 6 / RFC 6749 §5.1): the
// token response `expires_in` (and the device_code refresh-token lifetime) used
// to be hardcoded to 3600 / 30 days regardless of the configured
// `tokens.access_token_ttl` / `tokens.refresh_token_ttl`. With a non-default
// TTL the advertised lifetime silently diverged from the token's real expiry,
// so clients would refresh too early or keep using a dead token.
//
// This test asserts the response `expires_in` equals the value the running
// server actually loaded from config (read here from config.dev.json) rather
// than a literal 3600 -- that way the assertion stays correct if the config
// ever changes, and a regression back to a hardcoded literal would fail it.
//
// Limitation (documented honestly): all shipped configs use 3600, so under the
// default config this test cannot distinguish "uses config value" from "uses
// hardcoded 3600" -- both yield 3600. It still guards against a literal that
// differs from the loaded config, and binds the behavior to config going
// forward. The authoritative proof that a non-default TTL propagates requires a
// test-specific config override, which is out of scope here.
//
// Fixture: CONFIDENTIAL client `backend-svc` (secret "test-secret",
// client_credentials grant -- apps/server/seed/dev_backend_client.sql), with
// read scope ensured idempotently below.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <json/json.h>

#include <chrono>
#include <fstream>
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

// Read tokens.access_token_ttl from the dev config so the expected expires_in
// tracks whatever the server loaded (not a hardcoded 3600). Returns 3600 if the
// config/value cannot be parsed (the documented default).
long long expectedAccessTokenTtl()
{
    // config.dev.json lives at apps/server/config/config.dev.json relative to
    // the repo root. The test binary's CWD is the tests build dir; resolve via
    // the OAUTH2_REPO_ROOT_DIR define injected by tests/CMakeLists.txt.
#ifdef OAUTH2_REPO_ROOT_DIR
    const std::string path =
      std::string(OAUTH2_REPO_ROOT_DIR) + "/apps/server/config/config.dev.json";
#else
    const std::string path = "apps/server/config/config.dev.json";
#endif
    std::ifstream in(path);
    if (!in.is_open())
        return 3600;
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, in, &root, &errs))
        return 3600;
    try
    {
        return root["oauth2"]["tokens"]["access_token_ttl"].asInt64();
    }
    catch (...)
    {
        return 3600;
    }
}

// Idempotently grant read to backend-svc so client_credentials can issue.
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
}  // namespace

DROGON_TEST(Integration_P1_TokenExpiry_ClientCredentials_ExpiresInMatchesConfig)
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

    const long long expected = expectedAccessTokenTtl();
    REQUIRE(expected > 0);

    auto resp = postTokenForm(std::string("grant_type=client_credentials&") + kCredentials);
    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k200OK);
    Json::Value body;
    REQUIRE(parseBody(resp, body));
    CHECK(body.isMember("access_token"));
    REQUIRE(body.isMember("expires_in"));
    CHECK(body["expires_in"].asInt64() == expected);
}
