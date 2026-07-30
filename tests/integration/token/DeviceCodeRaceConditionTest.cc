// tests/integration/token/DeviceCodeRaceConditionTest.cc
//
// P1 #3 (评审问题点有效性分析报告, review finding 3 / RFC 8628 + RFC 6749 §4.1.2):
// device_code redemption used to be check-then-delete (read status=='approved'
// -> issue tokens -> deleteBy in the success callback), which is non-atomic:
// two concurrent redemptions of the SAME approved device_code could both read
// 'approved' and both issue a token pair. The handler now atomically transitions
// approved -> consumed via UPDATE ... WHERE status='approved' RETURNING and
// gates issuance on the affected row, so exactly one concurrent winner issues.
//
// This test seeds one approved device_code, fires two concurrent redemptions,
// and asserts exactly one returns 200 + access_token while the other returns
// 400 invalid_grant ("already consumed or no longer approved").
//
// Seeding reuses the PUBLIC device-flow client `p1-test-pub-device` created by
// DeviceCodeClientAuthTest (idempotent ON CONFLICT DO NOTHING here too), so this
// test is self-sufficient regardless of test ordering.

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
constexpr const char *kPubClient = "p1-test-pub-device";

bool parseBody(const HttpResponsePtr &resp, Json::Value &out)
{
    const std::string body(resp->getBody());
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    return reader->parse(body.data(), body.data() + body.size(), &out, &errs);
}

HttpResponsePtr postTokenFormAsync(const std::string &body)
{
    // Synchronous-from-async: each call runs on its own future so two can be
    // in flight concurrently when launched from separate std::async tasks.
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

bool serverReachable()
{
    auto client = HttpClient::newHttpClient(kBaseUrl);
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath("/oauth2/token");
    req->setContentTypeCode(CT_APPLICATION_X_FORM);
    req->setBody("grant_type=client_credentials");
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        auto [result, resp] = client->sendRequest(req, /*timeout=*/5.0);
        if (result == ReqResult::Ok && resp != nullptr)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return false;
}

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

bool ensurePubDeviceClient()
{
    return execSql(
      "INSERT INTO oauth2_clients "
      "(client_id, client_type, client_secret, salt, name, redirect_uris, "
      "allowed_grant_types) VALUES ('" +
      std::string(kPubClient) +
      "', 'PUBLIC', '', '', 'P1 test public device client', '', "
      "'urn:ietf:params:oauth:grant-type:device_code') "
      "ON CONFLICT (client_id) DO NOTHING"
    );
}

bool seedApprovedDeviceCode(const std::string &rawDeviceCode)
{
    const std::string hash = authforge::drogon::utils::hashToken(rawDeviceCode);
    const int64_t expiresAt = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()
                              )
                                .count() +
                              3600;
    if (!execSql("DELETE FROM oauth2_device_codes WHERE device_code_hash = '" + hash + "'"))
        return false;
    const std::string userCode = hash.substr(0, 8);
    return execSql(
      "INSERT INTO oauth2_device_codes "
      "(device_code_hash, user_code, client_id, scope, status, user_id, "
      "expires_at) VALUES ('" +
      hash + "', '" + userCode + "', '" + kPubClient + "', 'read', 'approved', '1', " +
      std::to_string(expiresAt) + ")"
    );
}
}  // namespace

DROGON_TEST(Integration_P1_DeviceCode_ConcurrentRedemption_OnlyOneSucceeds)
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
    REQUIRE(ensurePubDeviceClient());

    // A single device_code shared by both concurrent redemptions. A fixed
    // string is safe: we DELETE+INSERT it immediately above, and the consume
    // is atomic so only one request transitions it to 'consumed'.
    const std::string deviceCode = "p1-race-device-code-fixed";
    REQUIRE(seedApprovedDeviceCode(deviceCode));

    const std::string form =
      "grant_type=urn:ietf:params:oauth:grant-type:device_code"
      "&device_code=" +
      deviceCode + "&client_id=" + kPubClient;

    // Fire two redemptions concurrently.
    auto futA = std::async(std::launch::async, [&]() { return postTokenFormAsync(form); });
    auto futB = std::async(std::launch::async, [&]() { return postTokenFormAsync(form); });
    auto respA = futA.get();
    auto respB = futB.get();
    REQUIRE(respA != nullptr);
    REQUIRE(respB != nullptr);

    auto isOk = [](const HttpResponsePtr &r) {
        if (r->getStatusCode() != k200OK)
            return false;
        Json::Value body;
        return parseBody(r, body) && body.isMember("access_token");
    };
    auto isConsumeLoss = [](const HttpResponsePtr &r) {
        if (r->getStatusCode() != k400BadRequest)
            return false;
        Json::Value body;
        return parseBody(r, body) && body["error"].asString() == "invalid_grant";
    };

    const bool aOk = isOk(respA);
    const bool bOk = isOk(respB);
    // Exactly one winner must issue; the other loses the consume race.
    CHECK(aOk != bOk);
    CHECK((aOk ? isConsumeLoss(respB) : isConsumeLoss(respA)));
}
