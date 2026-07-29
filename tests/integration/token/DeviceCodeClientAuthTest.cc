// tests/integration/token/DeviceCodeClientAuthTest.cc
//
// P1 #5 (评审问题点有效性分析报告, review finding 5 / RFC 8628 §3.4): device_code
// redemption previously only string-matched client_id against the device-code
// row, skipping client authentication. RFC 8628 defers to RFC 6749 §3.2.1:
// CONFIDENTIAL clients MUST authenticate at the token endpoint; PUBLIC clients
// only need to identify themselves. The handler now branches on client_type:
//   - CONFIDENTIAL without a valid client_secret -> 401 invalid_client
//   - CONFIDENTIAL with the correct secret        -> token issued
//   - PUBLIC (client_id only)                      -> token issued
//
// Seeding: two device-flow clients are created idempotently (ON CONFLICT DO
// NOTHING): a CONFIDENTIAL one (`p1-test-conf-device`) and a PUBLIC one
// (`p1-test-pub-device`), each with a matching approved device_code row. The
// device_code_hash stored in oauth2_device_codes is computed with the same
// authforge::drogon::utils::hashToken the server uses, so a raw device_code
// sent by the client matches the stored hash. Rows are cleaned (DELETE) before
// each insert so the test is re-runnable.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <authforge/drogon/utils/CryptoUtils.h>
#include <json/json.h>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>

using namespace drogon;
using namespace drogon::orm;

namespace
{
constexpr const char *kBaseUrl = "http://127.0.0.1:5555";

constexpr const char *kConfClient = "p1-test-conf-device";
constexpr const char *kConfSecret = "p1-test-conf-device-secret";
constexpr const char *kPubClient = "p1-test-pub-device";

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
        auto [result, resp] = client->sendRequest(req, /*timeout=*/10.0);
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

// Run a SQL statement with no result; returns false on DB error.
bool execSql(const std::string &sql)
{
    auto db = app().getDbClient();
    if (!db)
        return false;
    std::promise<bool> p;
    db->execSqlAsync(
      sql,
      [&](const Result &) { p.set_value(true); },
      [&](const DrogonDbException &e) {
          LOG_ERROR << "execSql failed: " << e.base().what() << " :: " << sql;
          p.set_value(false);
      }
    );
    return p.get_future().get();
}

bool ensureDeviceClients()
{
    // CONFIDENTIAL device-flow client. The server's validateClient computes
    // getSha256(clientSecret + salt) and compares it (case-insensitively)
    // against the stored client_secret. Seed that exact hash so a correct
    // secret validates.
    const std::string confHash =
      ::drogon::utils::getSha256(std::string(kConfSecret) + "p1salt");

    bool ok = true;
    ok &= execSql(
      "INSERT INTO oauth2_clients "
      "(client_id, client_type, client_secret, salt, name, redirect_uris, "
      "allowed_grant_types) VALUES ('" +
      std::string(kConfClient) +
      "', 'CONFIDENTIAL', '" + confHash + "', 'p1salt', "
      "'P1 test confidential device client', '', "
      "'urn:ietf:params:oauth:grant-type:device_code') "
      "ON CONFLICT (client_id) DO UPDATE SET "
      "client_secret = EXCLUDED.client_secret, salt = EXCLUDED.salt");
    ok &= execSql(
      "INSERT INTO oauth2_clients "
      "(client_id, client_type, client_secret, salt, name, redirect_uris, "
      "allowed_grant_types) VALUES ('" +
      std::string(kPubClient) +
      "', 'PUBLIC', '', '', 'P1 test public device client', '', "
      "'urn:ietf:params:oauth:grant-type:device_code') "
      "ON CONFLICT (client_id) DO NOTHING");
    return ok;
}

// Monotonic per-process counter so each seeded device_code is unique without
// relying on rand() (keeps the test deterministic and avoids <cstdlib>).
long long nextDeviceCodeSeq()
{
    static std::atomic<long long> seq{0};
    return ++seq;
}

// Insert an approved, non-expired device_code row for `clientId` whose stored
// hash matches `rawDeviceCode` (hash computed with the server's hashToken).
// Cleans any prior row with the same hash first (re-runnable).
bool seedApprovedDeviceCode(const std::string &clientId, const std::string &rawDeviceCode)
{
    const std::string hash = authforge::drogon::utils::hashToken(rawDeviceCode);
    const int64_t expiresAt =
      std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
      )
        .count() +
      3600;
    if (!execSql("DELETE FROM oauth2_device_codes WHERE device_code_hash = '" + hash + "'"))
        return false;
    // user_code must be unique per row; derive a short suffix from the hash.
    const std::string userCode = hash.substr(0, 8);
    return execSql(
      "INSERT INTO oauth2_device_codes "
      "(device_code_hash, user_code, client_id, scope, status, user_id, "
      "expires_at) VALUES ('" +
      hash + "', '" + userCode + "', '" + clientId +
      "', 'read', 'approved', '1', " + std::to_string(expiresAt) + ")"
    );
}
}  // namespace

// CONFIDENTIAL device-flow client WITHOUT a secret -> rejected (invalid_client).
DROGON_TEST(Integration_P1_DeviceCode_ConfidentialClient_RequiresSecret)
{
    auto plugin = app().getPlugin<OAuth2Plugin>();
    if (!plugin || plugin->getStorageType() == "memory")
    {
        CHECK(true);
        return;
    }
    if (!serverReachable())
    {
        LOG_INFO << "Skipping: HTTP listener not reachable on " << kBaseUrl;
        CHECK(true);
        return;
    }
    REQUIRE(ensureDeviceClients());
    const std::string deviceCode = "p1-conf-no-secret-" + std::to_string(nextDeviceCodeSeq());
    REQUIRE(seedApprovedDeviceCode(kConfClient, deviceCode));

    auto resp = postTokenForm(
      "grant_type=urn:ietf:params:oauth:grant-type:device_code"
      "&device_code=" +
      deviceCode + "&client_id=" + kConfClient
    );
    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k401Unauthorized);
    Json::Value body;
    REQUIRE(parseBody(resp, body));
    CHECK(body["error"].asString() == "invalid_client");
    CHECK(!body.isMember("access_token"));
}

// CONFIDENTIAL device-flow client WITH the correct secret -> token issued.
DROGON_TEST(Integration_P1_DeviceCode_ConfidentialClient_ValidSecretSucceeds)
{
    auto plugin = app().getPlugin<OAuth2Plugin>();
    if (!plugin || plugin->getStorageType() == "memory")
    {
        CHECK(true);
        return;
    }
    if (!serverReachable())
    {
        LOG_INFO << "Skipping: HTTP listener not reachable on " << kBaseUrl;
        CHECK(true);
        return;
    }
    REQUIRE(ensureDeviceClients());
    const std::string deviceCode = "p1-conf-secret-" + std::to_string(nextDeviceCodeSeq());
    REQUIRE(seedApprovedDeviceCode(kConfClient, deviceCode));

    auto resp = postTokenForm(
      "grant_type=urn:ietf:params:oauth:grant-type:device_code"
      "&device_code=" +
      deviceCode + "&client_id=" + kConfClient + "&client_secret=" + kConfSecret
    );
    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k200OK);
    Json::Value body;
    REQUIRE(parseBody(resp, body));
    CHECK(body.isMember("access_token"));
}

// PUBLIC device-flow client (client_id only, no secret) -> token issued.
DROGON_TEST(Integration_P1_DeviceCode_PublicClient_NoSecretSucceeds)
{
    auto plugin = app().getPlugin<OAuth2Plugin>();
    if (!plugin || plugin->getStorageType() == "memory")
    {
        CHECK(true);
        return;
    }
    if (!serverReachable())
    {
        LOG_INFO << "Skipping: HTTP listener not reachable on " << kBaseUrl;
        CHECK(true);
        return;
    }
    REQUIRE(ensureDeviceClients());
    const std::string deviceCode = "p1-pub-" + std::to_string(nextDeviceCodeSeq());
    REQUIRE(seedApprovedDeviceCode(kPubClient, deviceCode));

    auto resp = postTokenForm(
      "grant_type=urn:ietf:params:oauth:grant-type:device_code"
      "&device_code=" +
      deviceCode + "&client_id=" + kPubClient
    );
    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k200OK);
    Json::Value body;
    REQUIRE(parseBody(resp, body));
    CHECK(body.isMember("access_token"));
}
