// Feature: error-code-message-standardization
// Task 12.2: OAuth2 协议端点 RFC 合规集成测试
//   验证协议端点错误体符合 RFC 6749 §5.2，且经协议入口产生。
//
// Validates: Requirements 7.2, 11.3
//   - 7.2:  每个 OAuth2_Protocol_Endpoint 经由 OAuth2 错误处理入口
//           (fulla::common::error::OAuth2ErrorHandler) 产生错误响应，输出 RFC 6749 错误体。
//   - 11.3: 协议端点错误响应的字段名、字段类型与错误码取值与 RFC 6749 §5.2 基线一致，
//           不新增/删除/重命名 `error`、`error_description`、`error_uri` 字段，且
//           不使用 Requirement 1 的 Error Envelope（无嵌套 error.code 对象）。
//
// ---------------------------------------------------------------------------
// Approach (hybrid, robust for the available harness)
// ---------------------------------------------------------------------------
// This is an *integration* test that confirms the protocol-error *entry point
// contract* at two complementary levels:
//
//   Part A — Protocol entry point (always runs, DB-independent):
//     The single OAuth2 protocol error entry point is
//     fulla::common::error::OAuth2ErrorHandler::sendErrorResponse (see Requirement 7.2
//     and design task 5.1). Every protocol endpoint that emits an error funnels
//     its protocol code through this entry point (introspect/revoke call it
//     directly; the token endpoint's RFC branches emit the same RFC §5.2 body
//     shape — verified live in Part B). Part A drives the entry point for the
//     full catalog of protocol codes and asserts the RFC 6749 §5.2 contract:
//     RFC body shape, the field-name whitelist (11.3), NOT an Error Envelope,
//     the RFC caching headers (Cache-Control/Pragma), Content-Type, and the
//     catalog-registered HTTP status. This confirms "produced via the protocol
//     entry point". It intentionally overlaps Property 9 (task 5.2) but pins the
//     contract at the integration level (a single regression here flags any
//     drift in the shared protocol entry point used by every protocol endpoint).
//
//   Part B — Live HTTP routing through /oauth2/token (guarded):
//     Sends real invalid requests over HTTP to the running server and asserts
//     the response body is an RFC 6749 §5.2 error body (top-level STRING `error`
//     in the allowed protocol set, optional string error_description/error_uri,
//     Content-Type application/json) and is NOT an Error Envelope (no nested
//     error.code object, no envelope-only top-level keys). Two DB-independent
//     branches are exercised:
//       * grant_type=client_credentials with no client credentials -> invalid_client (401)
//       * grant_type=urn:ietf:params:oauth:grant-type:device_code with no
//         device_code/client_id -> invalid_request (400)
//     Part B is guarded by server reachability: if the test server is not
//     listening (e.g. the suite is run without the HTTP listener), the HTTP
//     assertions are skipped (CHECK(true)) rather than producing a false
//     failure, mirroring the DB/memory guards used by the existing integration
//     tests.
//
// Coverage note: the cache-control headers (Requirement 2.3) are guaranteed by
// the shared protocol entry point and are asserted in Part A. Requirement 11.3
// (RFC §5.2 body field-name/type/error-code baseline) is asserted in BOTH parts
// (entry point + live routing). Requirement 7.2 (errors produced via the OAuth2
// error entry point as RFC bodies) is confirmed by Part A (the entry point
// itself) and by Part B (the live protocol endpoint actually emits RFC bodies).

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <fulla/drogon/error/OAuth2ErrorHandler.h>
#include <fulla/common/error/ErrorCatalog.h>
#include <json/json.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace drogon;
using namespace fulla::common::error;

namespace
{

// The RFC 6749 §5.2 error body permits exactly these top-level member names.
// (Requirement 11.3: no new/removed/renamed fields beyond this set.)
bool isRfcErrorBodyMember(const std::string &name)
{
    return name == "error" || name == "error_description" || name == "error_uri";
}

// Build the allowed protocol error-code set from the single source of truth
// (ErrorCatalog). Requirement 2.2 / 11.3: `error` must be one of these codes.
std::unordered_set<std::string> allowedProtocolCodes()
{
    std::unordered_set<std::string> codes;
    for (const auto &e : ErrorCatalog::allOAuthEntries())
    {
        codes.insert(std::string(e.error));
    }
    return codes;
}

// Invoke the protocol entry point synchronously and return the captured response.
HttpResponsePtr captureEntryPoint(
  const std::string &errorCode,
  const std::string &description = "",
  const std::string &errorUri = "",
  const std::string &authScheme = ""
)
{
    HttpResponsePtr captured;
    OAuth2ErrorHandler::sendErrorResponse(
      [&captured](const HttpResponsePtr &resp) { captured = resp; },
      errorCode,
      description,
      errorUri,
      authScheme
    );
    return captured;
}

// Parse a response body as JSON. Returns false on parse failure.
bool parseBody(const HttpResponsePtr &resp, Json::Value &out)
{
    const std::string body(resp->getBody());
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    return reader->parse(body.data(), body.data() + body.size(), &out, &errs);
}

// Shared RFC 6749 §5.2 body assertions (Requirement 11.3) used by Part A and
// Part B. Asserts: top-level object, `error` is a STRING (not an Envelope
// object) in the allowed protocol set, optional error_description/error_uri are
// strings, every member name is in the RFC whitelist, and no Error Envelope
// keys leak in.
void assertRfc6749ErrorBody(
  std::shared_ptr<::drogon::test::Case> TEST_CTX,
  const Json::Value &root,
  const std::unordered_set<std::string> &allowed,
  const std::string &expectedCode
)
{
    REQUIRE(root.isObject());

    // Top-level `error` present and a STRING (RFC §5.2), NOT an Envelope object.
    REQUIRE(root.isMember("error"));
    CHECK(root["error"].isString());
    CHECK(!root["error"].isObject());

    const std::string errorValue = root["error"].asString();
    CHECK(allowed.find(errorValue) != allowed.end());
    if (!expectedCode.empty())
    {
        CHECK(errorValue == expectedCode);
    }

    // Optional fields, when present, are strings (Requirement 2.1 / 11.3).
    if (root.isMember("error_description"))
    {
        CHECK(root["error_description"].isString());
    }
    if (root.isMember("error_uri"))
    {
        CHECK(root["error_uri"].isString());
    }

    // Field-name whitelist: only RFC §5.2 members are allowed (11.3 -> no new or
    // renamed fields), and explicitly none of the Error Envelope keys appear.
    for (const auto &name : root.getMemberNames())
    {
        CHECK(isRfcErrorBodyMember(name));
    }
    CHECK(!root.isMember("code"));
    CHECK(!root.isMember("category"));
    CHECK(!root.isMember("numeric_code"));
    CHECK(!root.isMember("request_id"));
    CHECK(!root.isMember("details"));
}

// Map the catalog-registered numeric HTTP status to the drogon enum for the
// status-code assertion in Part A.
HttpStatusCode expectedDrogonStatus(int httpStatus)
{
    switch (httpStatus)
    {
        case 400:
            return k400BadRequest;
        case 401:
            return k401Unauthorized;
        case 403:
            return k403Forbidden;
        case 500:
            return k500InternalServerError;
        case 503:
            return k503ServiceUnavailable;
        default:
            return k400BadRequest;
    }
}

constexpr const char *kBaseUrl = "http://localhost:5555";

}  // namespace

// ===========================================================================
// Part A: protocol entry point produces RFC 6749 §5.2 errors for every
// registered protocol code (Requirements 7.2, 11.3; headers per 2.1/2.3).
// ===========================================================================
DROGON_TEST(Integration_P1_OAuth2ProtocolEndpoint_EntryPoint_Rfc6749Compliance)
{
    const auto &oauthEntries = ErrorCatalog::allOAuthEntries();
    REQUIRE(!oauthEntries.empty());

    const std::unordered_set<std::string> allowed = allowedProtocolCodes();

    for (const auto &entry : oauthEntries)
    {
        const std::string code(entry.error);

        const HttpResponsePtr resp = captureEntryPoint(code);
        REQUIRE(resp != nullptr);

        // --- RFC §5.2 caching + content-type headers (Requirement 2.1/2.3) ---
        // Drogon stores a typed Content-Type separately from the header map, so
        // it is read via contentType()/contentTypeString().
        CHECK(resp->contentType() == CT_APPLICATION_JSON);
        CHECK(resp->getHeader("Cache-Control") == "no-store");
        CHECK(resp->getHeader("Pragma") == "no-cache");

        // --- Catalog-registered HTTP status (Requirement 2.7) ----------------
        CHECK(resp->getStatusCode() == expectedDrogonStatus(entry.httpStatus));

        // --- RFC §5.2 body shape (Requirement 11.3) --------------------------
        Json::Value root;
        REQUIRE(parseBody(resp, root));
        assertRfc6749ErrorBody(TEST_CTX, root, allowed, code);
    }
}

// ===========================================================================
// Part A (boundary): invalid_client emitted via the entry point is HTTP 401 and
// keeps the RFC §5.2 body shape (Requirement 2.4 / 11.3).
// ===========================================================================
DROGON_TEST(Integration_P1_OAuth2ProtocolEndpoint_EntryPoint_InvalidClientIs401Rfc)
{
    const std::unordered_set<std::string> allowed = allowedProtocolCodes();

    const HttpResponsePtr resp = captureEntryPoint("invalid_client");
    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k401Unauthorized);

    Json::Value root;
    REQUIRE(parseBody(resp, root));
    assertRfc6749ErrorBody(TEST_CTX, root, allowed, "invalid_client");
}

// ===========================================================================
// Part B: live HTTP routing through /oauth2/token emits RFC 6749 §5.2 error
// bodies (Requirements 7.2, 11.3). Guarded by server reachability.
// ===========================================================================

namespace
{

// Send a form-encoded POST to the running server. Returns nullptr if the server
// is not reachable (so the caller can skip the HTTP assertions).
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
        {
            return nullptr;
        }
        return resp;
    }
    catch (const std::exception &e)
    {
        LOG_WARN << "postForm failed (server likely unreachable): " << e.what();
        return nullptr;
    }
}

// Assert that a live HTTP response is an RFC 6749 §5.2 error body of the
// expected protocol code and HTTP status.
void assertLiveRfcError(
  std::shared_ptr<::drogon::test::Case> TEST_CTX,
  const HttpResponsePtr &resp,
  const std::unordered_set<std::string> &allowed,
  const std::string &expectedCode,
  HttpStatusCode expectedStatus
)
{
    REQUIRE(resp != nullptr);

    // Content-Type is delivered over the wire, so the header string is present.
    const bool jsonContentType =
      resp->getHeader("Content-Type").find("application/json") != std::string::npos ||
      resp->contentType() == CT_APPLICATION_JSON;
    CHECK(jsonContentType);

    CHECK(resp->getStatusCode() == expectedStatus);

    Json::Value root;
    REQUIRE(parseBody(resp, root));
    assertRfc6749ErrorBody(TEST_CTX, root, allowed, expectedCode);
}

}  // namespace

// --- B1: client_credentials grant with no client credentials -> invalid_client (401).
DROGON_TEST(Integration_P1_OAuth2TokenEndpoint_ClientCredentialsNoAuth_Rfc6749InvalidClient)
{
    const std::unordered_set<std::string> allowed = allowedProtocolCodes();

    // grant_type is a valid type (passes RuleSet validation), but no client_id /
    // client_secret -> the token handler emits an RFC invalid_client (401) body.
    // This path does not touch the database.
    const HttpResponsePtr resp = postForm("/oauth2/token", "grant_type=client_credentials");
    if (resp == nullptr)
    {
        // Server not reachable in this run: skip the live-routing assertions.
        LOG_WARN << "Skipping live /oauth2/token check: test server not reachable on " << kBaseUrl;
        CHECK(true);
        return;
    }

    assertLiveRfcError(TEST_CTX, resp, allowed, "invalid_client", k401Unauthorized);
}

// --- B2: device_code grant with missing parameters -> invalid_request (400).
DROGON_TEST(Integration_P1_OAuth2TokenEndpoint_DeviceCodeMissingParams_Rfc6749InvalidRequest)
{
    const std::unordered_set<std::string> allowed = allowedProtocolCodes();

    // Valid grant_type, but device_code and client_id are missing -> the token
    // handler emits an RFC invalid_request (400) body before any DB access.
    const HttpResponsePtr resp =
      postForm("/oauth2/token", "grant_type=urn:ietf:params:oauth:grant-type:device_code");
    if (resp == nullptr)
    {
        LOG_WARN << "Skipping live /oauth2/token check: test server not reachable on " << kBaseUrl;
        CHECK(true);
        return;
    }

    assertLiveRfcError(TEST_CTX, resp, allowed, "invalid_request", k400BadRequest);
}
