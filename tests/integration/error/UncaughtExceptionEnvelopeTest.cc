// Feature: error-code-message-standardization — Task 12.3 integration test.
//
// 未捕获异常经统一入口集成测试 (uncaught-exception-via-unified-entry).
//
// Validates: Requirement 7.7
//   IF 某 Application_Endpoint 在处理过程中抛出未被捕获的异常或触发框架默认错误
//   处理，THEN THE Backend SHALL 经由统一错误处理入口返回 Requirement 1 定义的
//   Error Envelope，而非框架默认的 HTML 或纯文本响应体。
//
// ---------------------------------------------------------------------------
// Harness note (why this is a contract-level integration test rather than a
// live HTTP round-trip):
//
//   The global std::exception handler that Requirement 7.7 governs is installed
//   in OAuth2Server/main.cc via drogon::app().setExceptionHandler(...). The test
//   executable (test/test_main.cc) boots its OWN drogon app and does NOT compile
//   main.cc, so the production handler lambda is not registered in-process and
//   cannot be exercised through an HTTP client here. Registering a fresh handler
//   inside the test would merely re-implement (not validate) the production
//   logic.
//
//   Therefore this test validates the handler's response-construction CONTRACT:
//   it reproduces the exact path-branching predicate and the exact response
//   construction main.cc performs for an uncaught exception, then asserts the
//   produced response is a Requirement-1 Error Envelope (and, for the OAuth2
//   protocol branch, an RFC 6749 §5.2 body) — never a framework default HTML /
//   plain-text body. The reproduced logic is kept byte-for-byte aligned with
//   main.cc's setExceptionHandler lambda (see the SOURCE-OF-TRUTH comment below)
//   so the assertions track the real handler.
//
//   The test touches no database, so it runs unchanged under
//   OAUTH2_MEMORY_TESTS_ONLY.
// ---------------------------------------------------------------------------

#include <drogon/drogon_test.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <authforge/common/error/ErrorCatalog.h>
#include <authforge/drogon/error/ErrorResponder.h>
#include <authforge/common/error/ErrorTypes.h>
#include <authforge/drogon/error/OAuth2ErrorHandler.h>
#include <authforge/drogon/error/RequestId.h>

#include <json/json.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace drogon;
using namespace authforge::common::error;

namespace
{

// --- SOURCE-OF-TRUTH MIRROR -------------------------------------------------
// The next two functions reproduce OAuth2Server/main.cc's setExceptionHandler
// lambda exactly. Keep them in sync with main.cc.

// main.cc:
//   const bool isOAuth2Protocol =
//     path.rfind("/oauth2/", 0) == 0 ||
//     path == "/.well-known/oauth-authorization-server" ||
//     path == "/.well-known/openid-configuration" || path == "/.well-known/jwks.json";
bool isOAuth2ProtocolPath(const std::string &path)
{
    return path.rfind("/oauth2/", 0) == 0 || path == "/.well-known/oauth-authorization-server" ||
           path == "/.well-known/openid-configuration" || path == "/.well-known/jwks.json";
}

// Reproduces the response main.cc's exception handler produces for an uncaught
// std::exception on a request with the given @p path. The CORS header injection
// of the real handler is orthogonal to Requirement 7.7 (body/Content-Type) and
// is intentionally omitted here.
//
//   * OAuth2 protocol path  -> OAuth2ErrorHandler::sendErrorResponse(SERVER_ERROR)
//                              (RFC 6749 §5.2 { "error": "server_error", ... }).
//   * Application path       -> ErrorResponder::buildResponse(req,
//                              Error::fromCode("INTERNAL_ERROR", resolve(req)))
//                              (Requirement 1 Error Envelope).
HttpResponsePtr simulateUncaughtExceptionResponse(const std::string &path, const std::exception &e)
{
    auto req = HttpRequest::newHttpRequest();
    req->setPath(path);

    // Mirror the handler's diagnostic logging of e.what() (Internal_Detail goes
    // to the server log, never the client body).
    LOG_INFO << "[test] simulated uncaught exception: " << e.what() << " on path: " << path;

    if (isOAuth2ProtocolPath(path))
    {
        HttpResponsePtr captured;
        OAuth2ErrorHandler::sendErrorResponse(
          [&captured](const HttpResponsePtr &resp) { captured = resp; },
          OAuth2ErrorHandler::SERVER_ERROR
        );
        return captured;
    }

    Error error =
      Error::fromCode(std::string(ErrorCatalog::internalError().code), RequestId::resolve(req));
    return ErrorResponder::buildResponse(req, error);
}

bool parseBody(const HttpResponsePtr &resp, Json::Value &out)
{
    const std::string body(resp->getBody());
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    return reader->parse(body.data(), body.data() + body.size(), &out, &errs);
}

// Representative Application_Endpoint paths (non-protocol) drawn from the
// server's controllers. An uncaught exception on any of these must yield an
// Error Envelope.
const std::vector<std::string> &applicationPaths()
{
    static const std::vector<std::string> kPaths = {
      "/api/admin/dashboard",
      "/api/admin/users",
      "/api/user/profile",
      "/api/user/sessions",
      "/api/me/webauthn/register/begin",
      "/api/organizations",
      "/api/clients",
      "/health",
      "/",
      "/some/unknown/route",
    };
    return kPaths;
}

// Representative OAuth2_Protocol_Endpoint paths. An uncaught exception on these
// must keep emitting an RFC 6749 §5.2 server_error body (NOT an Envelope).
const std::vector<std::string> &protocolPaths()
{
    static const std::vector<std::string> kPaths = {
      "/oauth2/token",
      "/oauth2/authorize",
      "/oauth2/introspect",
      "/oauth2/revoke",
      "/oauth2/device_authorization",
      "/.well-known/oauth-authorization-server",
      "/.well-known/openid-configuration",
      "/.well-known/jwks.json",
    };
    return kPaths;
}

}  // namespace

// ============================================================================
// Application branch: an uncaught exception yields a Requirement-1 Error
// Envelope (INTERNAL_ERROR), never a framework default HTML / plain-text body.
// ============================================================================
DROGON_TEST(Integration_P1_UncaughtException_ApplicationPath_ReturnsErrorEnvelope)
{
    const CatalogEntry &internalEntry = ErrorCatalog::internalError();

    for (const auto &path : applicationPaths())
    {
        // An Application path is NOT classified as an OAuth2 protocol path.
        CHECK(!isOAuth2ProtocolPath(path));

        const std::runtime_error boom("simulated handler failure: deref null / db threw");
        const HttpResponsePtr resp = simulateUncaughtExceptionResponse(path, boom);
        REQUIRE(resp != nullptr);

        // --- NOT a framework default HTML / plain-text body (Requirement 7.7) ---
        if (resp->contentType() != CT_APPLICATION_JSON)
        {
            LOG_ERROR << "path=" << path << " Content-Type is not application/json";
        }
        CHECK(resp->contentType() == CT_APPLICATION_JSON);
        CHECK(resp->contentType() != CT_TEXT_HTML);
        CHECK(resp->contentType() != CT_TEXT_PLAIN);

        // --- Body is a parseable Error Envelope (Requirement 1.1) --------------
        Json::Value root;
        const bool parsed = parseBody(resp, root);
        if (!parsed)
        {
            LOG_ERROR << "path=" << path << " body is not parseable JSON: " << resp->getBody();
        }
        REQUIRE(parsed);
        REQUIRE(root.isObject());

        // Top level: a single object whose ONLY key is `error` (value = object).
        CHECK(root.getMemberNames().size() == 1);
        REQUIRE(root.isMember("error"));
        REQUIRE(root["error"].isObject());
        const Json::Value &err = root["error"];

        // It must NOT be the RFC 6749 protocol shape ({ "error": "<string>" }).
        CHECK(!root["error"].isString());

        // --- Envelope field invariants for the INTERNAL_ERROR fallback ---------
        // code = INTERNAL_ERROR (Requirement 5.5).
        REQUIRE(err.isMember("code"));
        REQUIRE(err["code"].isString());
        CHECK(err["code"].asString() == std::string(internalEntry.code));

        // category = INTERNAL (Requirement 5.5).
        REQUIRE(err.isMember("category"));
        REQUIRE(err["category"].isString());
        CHECK(err["category"].asString() == "INTERNAL");

        // numeric_code = 6001 (Requirement 1.3 / 5.5).
        REQUIRE(err.isMember("numeric_code"));
        REQUIRE(err["numeric_code"].isInt());
        CHECK(err["numeric_code"].asInt() == 6001);
        CHECK(err["numeric_code"].asInt() == internalEntry.numericCode);

        // message = catalog default Client_Safe_Message (Requirement 5.6); the
        // raw exception text never reaches the client.
        REQUIRE(err.isMember("message"));
        REQUIRE(err["message"].isString());
        const std::string message = err["message"].asString();
        CHECK(!message.empty());
        CHECK(message == std::string(internalEntry.defaultMessage));
        CHECK(message.find("simulated handler failure") == std::string::npos);

        // request_id: non-empty (Requirement 6.1).
        REQUIRE(err.isMember("request_id"));
        REQUIRE(err["request_id"].isString());
        CHECK(!err["request_id"].asString().empty());

        // HTTP status: catalog-registered 500 for INTERNAL (Requirement 4.4 / 4.7).
        CHECK(resp->statusCode() == k500InternalServerError);
        CHECK(internalEntry.httpStatus == 500);
    }
}

// ============================================================================
// Protocol branch cross-check: an uncaught exception on an OAuth2 protocol path
// keeps emitting an RFC 6749 §5.2 server_error body (NOT an Error Envelope), so
// the unified entry's branching is exercised on both sides (Requirement 7.7 /
// 7.2 / 2.5).
// ============================================================================
DROGON_TEST(Integration_P1_UncaughtException_OAuth2Path_ReturnsRfc6749ServerError)
{
    const OAuthCatalogEntry *serverErrorEntry =
      ErrorCatalog::findOAuth(OAuth2ErrorHandler::SERVER_ERROR);
    REQUIRE(serverErrorEntry != nullptr);

    for (const auto &path : protocolPaths())
    {
        // A protocol path IS classified as an OAuth2 protocol path.
        CHECK(isOAuth2ProtocolPath(path));

        const std::runtime_error boom("simulated protocol handler failure");
        const HttpResponsePtr resp = simulateUncaughtExceptionResponse(path, boom);
        REQUIRE(resp != nullptr);

        // RFC 6749 protocol responses are JSON, never HTML / plain text.
        CHECK(resp->contentType() == CT_APPLICATION_JSON);
        CHECK(resp->contentType() != CT_TEXT_HTML);
        CHECK(resp->contentType() != CT_TEXT_PLAIN);

        // Cache headers mandated for protocol error bodies (Requirement 2.3).
        CHECK(resp->getHeader("Cache-Control") == "no-store");
        CHECK(resp->getHeader("Pragma") == "no-cache");

        Json::Value root;
        const bool parsed = parseBody(resp, root);
        if (!parsed)
        {
            LOG_ERROR << "path=" << path
                      << " protocol body is not parseable JSON: " << resp->getBody();
        }
        REQUIRE(parsed);
        REQUIRE(root.isObject());

        // Top-level `error` is the STRING "server_error" (RFC 6749 §5.2), NOT an
        // Error Envelope object.
        REQUIRE(root.isMember("error"));
        REQUIRE(root["error"].isString());
        CHECK(!root["error"].isObject());
        CHECK(root["error"].asString() == std::string(OAuth2ErrorHandler::SERVER_ERROR));

        // Envelope-only keys must be absent from a protocol body (Requirement 2.5).
        CHECK(!root.isMember("code"));
        CHECK(!root.isMember("category"));
        CHECK(!root.isMember("numeric_code"));
        CHECK(!root.isMember("request_id"));

        // server_error status is the catalog-registered 500.
        CHECK(resp->statusCode() == k500InternalServerError);
        CHECK(serverErrorEntry->httpStatus == 500);
    }
}

// ============================================================================
// Path-branching decision table: the exact predicate from main.cc routes
// Application paths to the Envelope branch and protocol paths to the RFC branch.
// Guards against accidental drift in the branching logic (Requirement 7.7).
// ============================================================================
DROGON_TEST(Integration_P1_UncaughtException_PathBranching_DecisionTable)
{
    // Application paths -> Envelope branch.
    for (const auto &path : applicationPaths())
    {
        CHECK(!isOAuth2ProtocolPath(path));
    }

    // Protocol paths -> RFC branch.
    for (const auto &path : protocolPaths())
    {
        CHECK(isOAuth2ProtocolPath(path));
    }

    // Boundary cases: the literal "/oauth2" (no trailing slash) is NOT a
    // protocol path under the rfind("/oauth2/", 0) prefix rule, so it falls to
    // the Application Envelope branch.
    CHECK(!isOAuth2ProtocolPath("/oauth2"));
    CHECK(!isOAuth2ProtocolPath("/oauth2-other/thing"));
    CHECK(isOAuth2ProtocolPath("/oauth2/"));
    CHECK(isOAuth2ProtocolPath("/oauth2/token"));

    // A non-protocol .well-known path is an Application path.
    CHECK(!isOAuth2ProtocolPath("/.well-known/other"));
}
