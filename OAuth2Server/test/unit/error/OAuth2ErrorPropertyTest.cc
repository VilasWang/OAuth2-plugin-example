// Feature: error-code-message-standardization, Property 9: OAuth2 协议端点 RFC 6749 合规
//
// Property 9 (design.md):
//   对任意 OAuth2 协议错误码，经协议入口产生的错误响应：顶层含字符串字段 `error`
//   （其值属于允许的协议错误码集合，且不取该集合之外的值），`error` 为字符串而非
//   Error Envelope 对象；存在 `error_uri` 时其为字符串；`error_description` 非空、
//   等于（或包含）Catalog 登记的默认值且不含 Internal_Detail；响应头含
//   `Content-Type: application/json`、`Cache-Control: no-store` 与 `Pragma: no-cache`。
//
// Validates: Requirements 2.1, 2.2, 2.3, 2.5, 2.8, 11.3
//
// Implementation notes:
//   * Hand-written random loop (>= 100 iterations) seeded with a fixed
//     std::mt19937 seed so failures are reproducible. The offending protocol
//     code / inputs and the seed are printed via LOG_ERROR before the failing
//     CHECK fires.
//   * The allowed protocol code set is taken from the single source of truth
//     ErrorCatalog::allOAuthEntries(), so the assertion that `error` stays
//     inside the allowed set is catalog-driven.
//   * OAuth2ErrorHandler::sendErrorResponse invokes its callback synchronously,
//     so the produced HttpResponsePtr is captured via the lambda and inspected.
//   * The JSON body is parsed with JsonCpp and asserted to be the RFC 6749 §5.2
//     shape ({ "error": <string>, ... }) and NOT the Error Envelope shape
//     ({ "error": { "code": ... } }).

#include <drogon/drogon_test.h>
#include <drogon/HttpResponse.h>
#include <authforge/drogon/error/OAuth2ErrorHandler.h>
#include <authforge/common/error/ErrorCatalog.h>
#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace drogon;
using namespace authforge::common::error;

namespace
{

// Fixed, printable seed so any failure is reproducible.
constexpr unsigned int kSeed = 0x0A07'2E99u;

// Invoke sendErrorResponse synchronously and return the captured response.
HttpResponsePtr capture(
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

// Parse the response body as JSON. Returns false on parse failure.
bool parseBody(const HttpResponsePtr &resp, Json::Value &out)
{
    const std::string body(resp->getBody());
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    return reader->parse(body.data(), body.data() + body.size(), &out, &errs);
}

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// Heuristic detection of Internal_Detail leakage (SQL statements, database
// driver text, file-system paths, stack-trace style fragments). The catalog
// defaults are Client_Safe_Message values, so this must never fire for them.
bool containsInternalDetail(const std::string &value)
{
    const std::string lower = toLower(value);

    // SQL / database driver markers.
    static const char *kSqlMarkers[] =
      {"select ",
       "insert ",
       "update ",
       "delete ",
       "drop ",
       " from ",
       " where ",
       " join ",
       "sqlstate",
       "syntax error",
       "duplicate key",
       "constraint \""};
    for (const char *m : kSqlMarkers)
    {
        if (lower.find(m) != std::string::npos)
        {
            return true;
        }
    }

    // File-system path / source-location markers.
    static const char *kPathMarkers[] =
      {".cc", ".cpp", ".hpp", "/var/", "/home/", "/usr/", "/etc/", "c:\\", "\\src\\"};
    for (const char *m : kPathMarkers)
    {
        if (lower.find(m) != std::string::npos)
        {
            return true;
        }
    }

    // Stack-trace style markers (hex addresses / frame markers).
    static const char *kStackMarkers[] = {"0x", "#0", "#1", "at 0x", "backtrace"};
    for (const char *m : kStackMarkers)
    {
        if (lower.find(m) != std::string::npos)
        {
            return true;
        }
    }

    return false;
}

// Clean, caller-supplied Client_Safe_Message candidates (Chinese, no
// Internal_Detail) used to exercise the "non-empty description is used verbatim"
// branch.
const std::vector<std::string> &cleanDescriptions()
{
    static const std::vector<std::string> kDescriptions = {
      "自定义的安全错误描述",
      "请重试或联系管理员",
      "参数校验未通过，请检查后重试",
      "当前操作无法完成",
    };
    return kDescriptions;
}

}  // namespace

// --- Main property test: random protocol code + description/error_uri variation.
DROGON_TEST(Property9_OAuth2Error_Rfc6749Compliance)
{
    LOG_INFO << "Property 9 OAuth2 RFC 6749 compliance test, fixed seed=0x" << std::hex << kSeed
             << std::dec;

    const auto &oauthEntries = ErrorCatalog::allOAuthEntries();
    REQUIRE(!oauthEntries.empty());

    // Allowed protocol code set (catalog is the single source of truth).
    std::unordered_set<std::string> allowedCodes;
    for (const auto &e : oauthEntries)
    {
        allowedCodes.insert(std::string(e.error));
    }

    std::mt19937 gen(kSeed);
    std::uniform_int_distribution<std::size_t> codeDist(0, oauthEntries.size() - 1);
    std::uniform_int_distribution<int> descModeDist(0, 1);  // 0 = empty (fallback), 1 = custom.
    std::uniform_int_distribution<int> uriModeDist(0, 1);   // 0 = none, 1 = custom.
    std::uniform_int_distribution<std::size_t> descPickDist(0, cleanDescriptions().size() - 1);

    constexpr int kIterations = 200;  // >= 100 per the PBT convention.
    for (int i = 0; i < kIterations; ++i)
    {
        const OAuthCatalogEntry &entry = oauthEntries[codeDist(gen)];
        const std::string code(entry.error);
        const std::string catalogDefault(entry.defaultErrorDesc);

        // Vary the description argument: empty -> expect catalog default fallback,
        // non-empty -> expect the caller value used verbatim.
        const bool useCustomDesc = descModeDist(gen) == 1;
        const std::string description = useCustomDesc ? cleanDescriptions()[descPickDist(gen)] : "";

        // Vary the error_uri argument: none vs a clean URI.
        const bool useCustomUri = uriModeDist(gen) == 1;
        const std::string errorUri = useCustomUri ? "https://example.com/oauth2/errors" : "";

        const HttpResponsePtr resp = capture(code, description, errorUri);
        if (resp == nullptr)
        {
            LOG_ERROR << "[seed=0x" << std::hex << kSeed << std::dec << " iter=" << i
                      << "] null response for code=" << code;
        }
        REQUIRE(resp != nullptr);

        // --- Headers (Requirements 2.1, 2.3) ---------------------------------
        // Content-Type: application/json (typed + raw header).
        if (resp->contentType() != CT_APPLICATION_JSON)
        {
            LOG_ERROR << "[seed=0x" << std::hex << kSeed << std::dec << " iter=" << i
                      << "] code=" << code << " contentType is not application/json";
        }
        CHECK(resp->contentType() == CT_APPLICATION_JSON);
        // Drogon stores Content-Type separately from the generic header map, so
        // the string form is read via contentTypeString() (getHeader("Content-Type")
        // returns empty for a typed content type).
        CHECK(toLower(resp->contentTypeString()).find("application/json") != std::string::npos);

        // Cache-Control: no-store, Pragma: no-cache.
        if (resp->getHeader("Cache-Control") != "no-store")
        {
            LOG_ERROR << "[seed=0x" << std::hex << kSeed << std::dec << " iter=" << i
                      << "] code=" << code << " Cache-Control=" << resp->getHeader("Cache-Control");
        }
        CHECK(resp->getHeader("Cache-Control") == "no-store");
        CHECK(resp->getHeader("Pragma") == "no-cache");

        // --- Body shape (Requirements 2.1, 2.2, 2.5, 2.8, 11.3) --------------
        Json::Value root;
        const bool parsed = parseBody(resp, root);
        if (!parsed)
        {
            LOG_ERROR << "[seed=0x" << std::hex << kSeed << std::dec << " iter=" << i
                      << "] code=" << code << " body is not valid JSON: " << resp->getBody();
        }
        REQUIRE(parsed);
        REQUIRE(root.isObject());

        // Top-level `error` is a STRING (RFC 6749 §5.2), NOT an Envelope object.
        REQUIRE(root.isMember("error"));
        if (!root["error"].isString())
        {
            LOG_ERROR << "[seed=0x" << std::hex << kSeed << std::dec << " iter=" << i
                      << "] code=" << code << " top-level error is not a string (Envelope leak?)";
        }
        CHECK(root["error"].isString());
        // Explicitly assert it is NOT the Error Envelope shape (no nested error.code object).
        CHECK(!root["error"].isObject());

        // `error` value is in the allowed protocol code set, and equals the code used.
        const std::string errorValue = root["error"].asString();
        if (allowedCodes.find(errorValue) == allowedCodes.end())
        {
            LOG_ERROR << "[seed=0x" << std::hex << kSeed << std::dec << " iter=" << i
                      << "] error value '" << errorValue << "' not in allowed protocol set";
        }
        CHECK(allowedCodes.find(errorValue) != allowedCodes.end());
        CHECK(errorValue == code);

        // `error_description` present, string, non-empty; equals/contains the
        // catalog default; never contains Internal_Detail.
        REQUIRE(root.isMember("error_description"));
        CHECK(root["error_description"].isString());
        const std::string desc = root["error_description"].asString();
        if (desc.empty())
        {
            LOG_ERROR << "[seed=0x" << std::hex << kSeed << std::dec << " iter=" << i
                      << "] code=" << code << " empty error_description";
        }
        CHECK(!desc.empty());

        if (useCustomDesc)
        {
            // Non-empty argument is used verbatim (Requirement 2.8 allows
            // equals/contains the default; a caller value is used as-is).
            CHECK(desc == description);
        }
        else
        {
            // Empty argument falls back to the catalog default (Requirement 2.8).
            if (desc != catalogDefault)
            {
                LOG_ERROR << "[seed=0x" << std::hex << kSeed << std::dec << " iter=" << i
                          << "] code=" << code << " error_description='" << desc
                          << "' != catalog default '" << catalogDefault << "'";
            }
            CHECK(desc == catalogDefault);
        }

        // Client_Safe_Message: no Internal_Detail leakage (Requirement 2.8).
        if (containsInternalDetail(desc))
        {
            LOG_ERROR << "[seed=0x" << std::hex << kSeed << std::dec << " iter=" << i
                      << "] code=" << code << " error_description leaks Internal_Detail: " << desc;
        }
        CHECK(!containsInternalDetail(desc));

        // `error_uri`, when present, MUST be a string (Requirement 2.1).
        if (root.isMember("error_uri"))
        {
            CHECK(root["error_uri"].isString());
            if (useCustomUri)
            {
                CHECK(root["error_uri"].asString() == errorUri);
            }
        }
        else
        {
            // With no caller URI and an empty catalog errorUri, the field is omitted.
            CHECK(!useCustomUri);
        }

        // Envelope-only keys must never appear in a protocol error body
        // (Requirement 2.5 / 11.3): the body keeps the RFC 6749 §5.2 shape.
        CHECK(!root.isMember("code"));
        CHECK(!root.isMember("category"));
        CHECK(!root.isMember("numeric_code"));
        CHECK(!root.isMember("request_id"));
    }
}

// --- Focused example: every allowed protocol code with an empty description
// falls back to its catalog default and yields the RFC 6749 §5.2 shape + headers.
DROGON_TEST(Property9_OAuth2Error_AllCodesFallbackToCatalogDefault)
{
    const auto &oauthEntries = ErrorCatalog::allOAuthEntries();
    REQUIRE(!oauthEntries.empty());

    for (const auto &entry : oauthEntries)
    {
        const std::string code(entry.error);
        const HttpResponsePtr resp = capture(code);
        REQUIRE(resp != nullptr);

        // Headers.
        CHECK(resp->contentType() == CT_APPLICATION_JSON);
        CHECK(resp->getHeader("Cache-Control") == "no-store");
        CHECK(resp->getHeader("Pragma") == "no-cache");

        // Body shape.
        Json::Value root;
        REQUIRE(parseBody(resp, root));
        REQUIRE(root.isObject());
        REQUIRE(root.isMember("error"));
        CHECK(root["error"].isString());
        CHECK(root["error"].asString() == code);

        // error_description equals the catalog default and is leak-free.
        REQUIRE(root.isMember("error_description"));
        CHECK(root["error_description"].isString());
        const std::string desc = root["error_description"].asString();
        CHECK(desc == std::string(entry.defaultErrorDesc));
        CHECK(!desc.empty());
        CHECK(!containsInternalDetail(desc));
    }
}
