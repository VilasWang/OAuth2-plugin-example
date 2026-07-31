// Feature: error-code-message-standardization — Error Envelope property tests.
//
// This file hosts the property-based tests that exercise the Error Envelope
// produced by authforge::common::error::Error::toJson(bool). It is intentionally split
// into clearly delimited sections so later tasks can append their property
// tests next to a shared set of helpers without re-declaring them:
//
//   * SHARED HELPERS                  — RNG-backed generators + JSON utilities
//                                       reused by every property in this file.
//   * Property 3  (task 2.2)          — Error Envelope serialization round-trip.
//   * Property 1  (task 4.2)          — Error Envelope structural invariants.    [later]
//   * Property 2  (task 4.3)          — numeric_code correctness & omission.      [later]
//   * Property 6  (task 4.5)          — production-mode safety isolation.         [later]
//   * Property 7  (task 4.6)          — non-production diagnostic details.        [later]
//   * Property 8  (task 4.7)          — unmapped-exception internal fallback.     [later]
//
// Backend property tests use Drogon's drogon_test.h (DROGON_TEST macro) with a
// hand-written random loop (>= 100 iterations) seeded by a fixed std::mt19937
// seed; on failure the offending input and seed are printed via LOG_ERROR so
// the counterexample is reproducible. Because test/CMakeLists.txt collects
// unit/*.cc with GLOB_RECURSE, this file is picked up automatically.

#include <drogon/drogon_test.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <authforge/common/error/ErrorTypes.h>
#include <authforge/common/error/ErrorCatalog.h>
#include <authforge/common/error/ErrorContext.h>
#include <authforge/drogon/error/ErrorResponder.h>
#include <json/json.h>

#include <cstdint>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace authforge::common::error;

// ============================================================================
// SHARED HELPERS — reused by all Error Envelope properties in this file.
// (Anonymous namespace: internal linkage, no ODR clashes across the suite.)
// ============================================================================
namespace
{

// trantor's LogStream has no overload for iostream manipulators: streaming
// std::hex would convert the function address to bool and print `1`
// (AppleClang -Wpointer-bool-conversion). Pre-format the seed instead.
std::string seedHex(unsigned int seed)
{
    std::ostringstream oss;
    oss << std::hex << seed;
    return oss.str();
}

// Serialize a Json::Value to a compact string using JsonCpp's StreamWriter.
// Matches the production write path (basic types only) so a round-trip through
// this writer mirrors what a client would receive on the wire.
std::string serializeJson(const Json::Value &value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";  // compact, single-line output.
    return Json::writeString(builder, value);
}

// Parse a JSON string with JsonCpp's CharReader (same convention as the other
// error property tests). Returns false on parse failure.
bool parseJson(const std::string &text, Json::Value &out)
{
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    return reader->parse(text.data(), text.data() + text.size(), &out, &errs);
}

// Generate a random string of length [minLen, maxLen] drawn from a wide set of
// printable code points: ASCII printable, a handful of CJK characters (valid
// UTF-8 multi-byte sequences) and JSON-significant characters (quotes,
// backslash, control-ish) so the serializer's escaping is exercised too.
std::string randomMessage(std::mt19937 &gen, std::size_t minLen, std::size_t maxLen)
{
    // A pool of valid UTF-8 tokens (single- and multi-byte). Each token is a
    // complete, well-formed UTF-8 sequence so the writer never sees invalid
    // bytes while still covering non-ASCII and escape-sensitive characters.
    static const std::vector<std::string> kTokens = {"a",  "Z",  "0",  "9",  " ",  "_",  "-",  ".",
                                                     "/",  ":",  ";",  ",",  "\"", "\\", "\t", "{",
                                                     "}",  "[",  "]",  "<",  ">",  "%",  "用", "户",
                                                     "名", "或", "密", "码", "错", "误", "服", "务",
                                                     "器", "无", "权", "限", "请", "求", "超", "时",
                                                     "授", "权", "失", "效"};

    std::uniform_int_distribution<std::size_t> lenDist(minLen, maxLen);
    std::uniform_int_distribution<std::size_t> tokDist(0, kTokens.size() - 1);

    const std::size_t len = lenDist(gen);
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i)
    {
        out += kTokens[tokDist(gen)];
    }
    return out;
}

// Generate a random valid-shaped Request_ID: non-empty, length 1..128, drawn
// from the agreed charset [A-Za-z0-9_-]. (Round-trip holds for any string, but
// a realistic Request_ID keeps the fixtures representative of production.)
std::string randomRequestId(std::mt19937 &gen)
{
    static const char kCharset[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
    constexpr std::size_t kCharsetSize = sizeof(kCharset) - 1;

    std::uniform_int_distribution<std::size_t> lenDist(1, 128);
    std::uniform_int_distribution<std::size_t> charDist(0, kCharsetSize - 1);

    const std::size_t len = lenDist(gen);
    std::string out;
    out.reserve(len);
    for (std::size_t i = 0; i < len; ++i)
    {
        out += kCharset[charDist(gen)];
    }
    return out;
}

// Build a random Error whose `code` is a registered Error_Code drawn from the
// catalog (every registered entry carries a numeric_code). The message and
// request_id are randomized; `details` is randomized and may be empty so the
// includeDetails=true/false paths and the empty/non-empty branches are all
// exercised by callers.
Error randomRegisteredError(std::mt19937 &gen, bool withDetails)
{
    const auto &entries = ErrorCatalog::allEntries();
    std::uniform_int_distribution<std::size_t> idxDist(0, entries.size() - 1);
    const CatalogEntry &entry = entries[idxDist(gen)];

    Error error;
    error.code = std::string(entry.code);
    error.category = entry.category;
    error.message = randomMessage(gen, 1, 120);
    error.details = withDetails ? randomMessage(gen, 1, 80) : std::string();
    error.requestId = randomRequestId(gen);
    return error;
}

}  // namespace

// ============================================================================
// Property 3: Error Envelope 序列化 round-trip  (task 2.2)
//
// 对任意有效的 Error（含或不含 numeric_code、含或不含 details），将其序列化为
// JSON 字符串后再反序列化，还原出的 code/category/message/numeric_code/request_id
// 字段取值与序列化前相等。
//
// Validates: Requirements 1.5, 12.3
//
// All catalog-registered codes carry a numeric_code, so this loop covers the
// "with numeric_code" case for every registered code; the includeDetails flag
// and an empty/non-empty `details` string cover the "with/without details"
// dimension. Note: per Error::toJson, `details` is only emitted when
// includeDetails && !details.empty(), and Property 3 compares only
// code/category/message/numeric_code/request_id — so details presence does not
// affect the round-trip assertions but is varied to exercise the encoder.
// ============================================================================
DROGON_TEST(Unit_P0_ErrorEnvelope_Property3_SerializationRoundTrip)
{
    // Fixed, printable seed so any failure is reproducible.
    constexpr unsigned int kSeed = 0xE12C0DEu;
    LOG_INFO << "Property 3 Error Envelope round-trip test, fixed seed=0x" << seedHex(kSeed);

    std::mt19937 gen(kSeed);

    REQUIRE(!ErrorCatalog::allEntries().empty());

    std::uniform_int_distribution<int> detailDist(0, 1);  // randomize details empty/non-empty.

    constexpr int kIterations = 200;  // >= 100 per the PBT convention.
    for (int i = 0; i < kIterations; ++i)
    {
        const bool withDetails = detailDist(gen) == 1;
        const Error original = randomRegisteredError(gen, withDetails);

        // Exercise both includeDetails branches across iterations.
        const bool includeDetails = (i % 2 == 0);

        // Encode -> string -> decode.
        const Json::Value encoded = original.toJson(includeDetails);
        const std::string wire = serializeJson(encoded);

        Json::Value decoded;
        const bool parsed = parseJson(wire, decoded);
        if (!parsed)
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] round-trip parse failed for code=" << original.code
                      << " includeDetails=" << includeDetails << " wire=" << wire;
        }
        REQUIRE(parsed);

        // Envelope shape: single top-level `error` object.
        REQUIRE(decoded.isObject());
        REQUIRE(decoded.isMember("error"));
        REQUIRE(decoded["error"].isObject());
        const Json::Value &err = decoded["error"];

        // --- code preserved ---------------------------------------------------
        if (!err.isMember("code") || err["code"].asString() != original.code)
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] code mismatch: expected='" << original.code << "' got='"
                      << err["code"].asString() << "'";
        }
        REQUIRE(err.isMember("code"));
        CHECK(err["code"].asString() == original.code);

        // --- category preserved ----------------------------------------------
        const std::string expectedCategory = toString(original.category);
        if (!err.isMember("category") || err["category"].asString() != expectedCategory)
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] category mismatch for code=" << original.code << ": expected='"
                      << expectedCategory << "' got='" << err["category"].asString() << "'";
        }
        REQUIRE(err.isMember("category"));
        CHECK(err["category"].asString() == expectedCategory);

        // --- message preserved (verbatim, incl. escaped / non-ASCII content) --
        if (!err.isMember("message") || err["message"].asString() != original.message)
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] message mismatch for code=" << original.code << ": expected='"
                      << original.message << "' got='" << err["message"].asString() << "'";
        }
        REQUIRE(err.isMember("message"));
        CHECK(err["message"].asString() == original.message);

        // --- request_id preserved --------------------------------------------
        if (!err.isMember("request_id") || err["request_id"].asString() != original.requestId)
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] request_id mismatch for code=" << original.code << ": expected='"
                      << original.requestId << "' got='" << err["request_id"].asString() << "'";
        }
        REQUIRE(err.isMember("request_id"));
        CHECK(err["request_id"].asString() == original.requestId);

        // --- numeric_code preserved (all registered codes have one) -----------
        REQUIRE(original.hasNumericCode());
        const int expectedNumeric = original.numericCode();
        if (!err.isMember("numeric_code") || err["numeric_code"].asInt() != expectedNumeric)
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] numeric_code mismatch for code=" << original.code
                      << ": expected=" << expectedNumeric
                      << " present=" << err.isMember("numeric_code")
                      << " got=" << err["numeric_code"].asInt();
        }
        REQUIRE(err.isMember("numeric_code"));
        CHECK(err["numeric_code"].asInt() == expectedNumeric);
    }
}

// ============================================================================
// Property 1: Error Envelope 结构不变量  (task 4.2)
//
// 对任意已在 Error_Catalog 中登记的 Error_Code，经统一入口产生的 Application 错误
// 响应，其 JSON 顶层有且仅有一个键 `error`（值为对象）；该 `error` 对象包含非空字符
// 串 `code`（且属于 Error_Catalog 已登记集合）、属于枚举集合 {NETWORK, DATABASE,
// VALIDATION, AUTHENTICATION, AUTHORIZATION, INTERNAL, UNKNOWN} 的字符串
// `category`、长度 1..500 的字符串 `message`、非空字符串 `request_id`；该对象的键集
// 合仅取自 {code, category, message, request_id, numeric_code, details, timestamp}
// （不含 error_description、reason 等别名）；且响应头 Content-Type 为
// application/json。
//
// Validates: Requirements 1.1, 1.2, 1.4, 1.6, 7.5
//
// How the response is produced: an Error is built via Error::fromCode(code,
// requestId) (fromCode sets the catalog default Client_Safe_Message, which is
// non-empty and within 1..500) for every registered code, then rendered both
//   (a) directly via Error::toJson(includeDetails) for the structural / key-set
//       / length / value checks (covering includeDetails = true and false), and
//   (b) via ErrorResponder::buildResponse(req, error) for the Content-Type and
//       end-to-end shape checks (the unified Application entry point).
// The catalog is iterated generically through ErrorCatalog::allEntries() so the
// entry count is never hard-coded. The detailed-errors override is forced on so
// the includeDetails=true branch genuinely emits `details`, then restored.
// ============================================================================
DROGON_TEST(Unit_P0_ErrorEnvelope_Property1_StructuralInvariants)
{
    // Fixed, printable seed so any failure is reproducible.
    constexpr unsigned int kSeed = 0x51'70C0DEu;
    LOG_INFO << "Property 1 Error Envelope structural-invariant test, fixed seed=0x"
             << seedHex(kSeed);

    std::mt19937 gen(kSeed);

    const auto &entries = ErrorCatalog::allEntries();
    REQUIRE(!entries.empty());

    // The only field names an Application Error Envelope's `error` object is
    // allowed to carry (Requirement 7.5). NO aliases (error_description/reason).
    static const std::unordered_set<std::string> kAllowedKeys =
      {"code", "category", "message", "request_id", "numeric_code", "details", "timestamp"};

    // The Error_Category enum string set (Requirement 1.2).
    static const std::unordered_set<std::string> kCategorySet = {
      "NETWORK", "DATABASE", "VALIDATION", "AUTHENTICATION", "AUTHORIZATION", "INTERNAL", "UNKNOWN"
    };

    // The set of every registered Error_Code, used to assert membership
    // (Requirement 1.6). Built generically from allEntries().
    std::unordered_set<std::string> registeredCodes;
    for (const auto &e : entries)
    {
        registeredCodes.insert(std::string(e.code));
    }

    // Force non-Production_Mode so includeDetails=true actually emits `details`,
    // exercising the largest possible key set against the whitelist. Restored
    // at the end so later property tests in this file see the default context.
    ErrorContext::setDetailedErrorsOverride(true);

    std::uniform_int_distribution<std::size_t> idxDist(0, entries.size() - 1);

    constexpr int kIterations = 200;  // >= 100 per the PBT convention.
    for (int i = 0; i < kIterations; ++i)
    {
        // Pick any registered code (sampled, not hard-coded by index/count).
        const CatalogEntry &entry = entries[idxDist(gen)];
        const std::string code(entry.code);
        const std::string requestId = randomRequestId(gen);

        // Build via the catalog-backed factory: fromCode sets the default
        // Client_Safe_Message (non-empty, within 1..500) and category.
        Error error = Error::fromCode(code, requestId);
        // Give `details` a non-empty value so the includeDetails=true path emits
        // the optional `details` key (still inside the whitelist).
        error.details = randomMessage(gen, 1, 80);

        // Cover both includeDetails branches across iterations.
        const bool includeDetails = (i % 2 == 0);

        // ---- (a) Structural / key-set / value checks via toJson -------------
        const Json::Value root = error.toJson(includeDetails);

        // Top level: a single object whose ONLY key is `error` (Requirement 1.1).
        REQUIRE(root.isObject());
        if (root.getMemberNames().size() != 1 || !root.isMember("error"))
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] top-level is not a single `error` key for code=" << code
                      << " keys=" << static_cast<int>(root.getMemberNames().size());
        }
        REQUIRE(root.getMemberNames().size() == 1);
        REQUIRE(root.isMember("error"));
        REQUIRE(root["error"].isObject());
        const Json::Value &err = root["error"];

        // Key set is a subset of the whitelist; no aliases (Requirement 7.5).
        for (const auto &key : err.getMemberNames())
        {
            if (kAllowedKeys.find(key) == kAllowedKeys.end())
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] code=" << code << " has disallowed key '" << key << "'";
            }
            CHECK(kAllowedKeys.find(key) != kAllowedKeys.end());
        }
        // Explicitly assert the known aliases are absent.
        CHECK(!err.isMember("error_description"));
        CHECK(!err.isMember("reason"));

        // `code`: non-empty string, in the registered catalog set (Req 1.2, 1.6).
        REQUIRE(err.isMember("code"));
        REQUIRE(err["code"].isString());
        const std::string codeValue = err["code"].asString();
        if (codeValue.empty() || registeredCodes.find(codeValue) == registeredCodes.end())
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] code value not registered / empty: '" << codeValue << "'";
        }
        CHECK(!codeValue.empty());
        CHECK(registeredCodes.find(codeValue) != registeredCodes.end());

        // `category`: string in the enum set (Requirement 1.2).
        REQUIRE(err.isMember("category"));
        REQUIRE(err["category"].isString());
        const std::string categoryValue = err["category"].asString();
        if (kCategorySet.find(categoryValue) == kCategorySet.end())
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] code=" << code << " category not in enum set: '" << categoryValue
                      << "'";
        }
        CHECK(kCategorySet.find(categoryValue) != kCategorySet.end());

        // `message`: string of length 1..500 (Requirement 1.2).
        REQUIRE(err.isMember("message"));
        REQUIRE(err["message"].isString());
        const std::string messageValue = err["message"].asString();
        if (messageValue.empty() || messageValue.size() > 500)
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] code=" << code
                      << " message length out of [1,500]: " << messageValue.size();
        }
        CHECK(!messageValue.empty());
        CHECK(messageValue.size() <= 500u);

        // `request_id`: non-empty string (Requirement 1.2 / 6.1).
        REQUIRE(err.isMember("request_id"));
        REQUIRE(err["request_id"].isString());
        if (err["request_id"].asString().empty())
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] code=" << code << " request_id is empty";
        }
        CHECK(!err["request_id"].asString().empty());

        // ---- (b) Content-Type via the unified Application entry point --------
        // buildResponse decides `details` from ErrorContext; with the override
        // on it behaves like non-Production_Mode. The Content-Type must be
        // application/json (Requirement 1.4).
        const ::drogon::HttpRequestPtr req = ::drogon::HttpRequest::newHttpRequest();
        const ::drogon::HttpResponsePtr resp = ErrorResponder::buildResponse(req, error);
        REQUIRE(resp != nullptr);
        if (resp->contentType() != ::drogon::CT_APPLICATION_JSON)
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] code=" << code << " Content-Type is not application/json";
        }
        CHECK(resp->contentType() == ::drogon::CT_APPLICATION_JSON);

        // The response body must itself be a parseable Error Envelope whose top
        // level has exactly the single `error` object key.
        Json::Value respRoot;
        const bool parsed = parseJson(std::string(resp->getBody()), respRoot);
        if (!parsed)
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] code=" << code << " response body is not parseable JSON";
        }
        REQUIRE(parsed);
        REQUIRE(respRoot.isObject());
        CHECK(respRoot.getMemberNames().size() == 1);
        CHECK(respRoot.isMember("error"));
        CHECK(respRoot["error"].isObject());
    }

    // Restore the default error context for subsequent property tests.
    ErrorContext::clearDetailedErrorsOverride();
}

// Feature: error-code-message-standardization, Property 2: numeric_code 正确性与省略
//
// Property 2: numeric_code 正确性与省略  (task 4.3)
//
// 对任意 Error_Code：
//   * 若该 code 已在 Error_Catalog 中登记（并因此拥有 Numeric_Error_Code），则其
//     Error Envelope 含 `numeric_code` 字段，且取值等于 Catalog 登记的整数码
//     （Requirement 1.3）。
//   * 若该 code 未登记（无对应整数码），则其 Error Envelope 完全省略 `numeric_code`
//     键 —— 既不是 null 也不是空字符串，而是该成员根本不存在（Requirement 1.7）。
//
// Validates: Requirements 1.3, 1.7
//
// 全部 14 个 Catalog 登记码都带有 numeric_code，因此 PRESENT 分支通过
// Error::fromCode(entry.code, ...) 逐一覆盖每个登记码；为触发 OMISSION 分支，必须
// 直接以 Error 结构体构造一个 *不在* Catalog 内的 code（绝不经 fromCode，否则会被
// 重映射到 INTERNAL_ERROR 而带上 numeric 6001）。两个分支都在 includeDetails 的两种
// 取值下断言，以证明 numeric_code 的出现与否独立于 details / 生产模式。Catalog 通过
// ErrorCatalog::allEntries() 泛型遍历，条目数量从不写死。本属性不依赖也不修改
// ErrorContext override，保持文件对后续 4.5/4.6/4.7 追加属性可干净复用。
DROGON_TEST(Unit_P0_ErrorEnvelope_Property2_NumericCodeCorrectnessAndOmission)
{
    // Fixed, printable seed so any failure is reproducible.
    constexpr unsigned int kSeed = 0x2'4D'C0DEu;
    LOG_INFO << "Property 2 numeric_code correctness/omission test, fixed seed=0x"
             << seedHex(kSeed);

    std::mt19937 gen(kSeed);

    const auto &entries = ErrorCatalog::allEntries();
    REQUIRE(!entries.empty());

    // Synthetic Error_Codes that are NOT registered in the catalog. These drive
    // the OMISSION branch; each is asserted to be unregistered before use so the
    // test fails loudly should one ever be added to the catalog.
    static const std::vector<std::string> kUnregisteredCodes =
      {"SOME_UNREGISTERED_CODE",
       "CUSTOM_AD_HOC_ERROR",
       "NOPE_NOT_REGISTERED",
       "X_UNKNOWN_THING",
       "未登记的错误码",
       "lower_case_code",
       "INTERNAL_ERROR_TYPO",
       "AUTH_",
       "_PREFIXED_CODE",
       "code with spaces"};
    for (const auto &code : kUnregisteredCodes)
    {
        REQUIRE(ErrorCatalog::find(code) == nullptr);
    }

    // Every ErrorCategory value, so synthetic (unregistered) errors carry a real
    // category just like a hand-built Error would.
    static const std::vector<ErrorCategory> kCategories =
      {ErrorCategory::NETWORK,
       ErrorCategory::DATABASE,
       ErrorCategory::VALIDATION,
       ErrorCategory::AUTHENTICATION,
       ErrorCategory::AUTHORIZATION,
       ErrorCategory::INTERNAL,
       ErrorCategory::UNKNOWN};

    std::uniform_int_distribution<std::size_t> entryDist(0, entries.size() - 1);
    std::uniform_int_distribution<std::size_t> unregDist(0, kUnregisteredCodes.size() - 1);
    std::uniform_int_distribution<std::size_t> catDist(0, kCategories.size() - 1);
    std::uniform_int_distribution<int> branchDist(0, 1);
    std::uniform_int_distribution<int> detailDist(0, 1);

    constexpr int kIterations = 200;  // >= 100 per the PBT convention.
    for (int i = 0; i < kIterations; ++i)
    {
        // Cover both includeDetails branches; numeric_code must be independent.
        const bool includeDetails = (detailDist(gen) == 1);

        if (branchDist(gen) == 0)
        {
            // ---- PRESENT branch: a registered code carries its numeric_code ---
            const CatalogEntry &entry = entries[entryDist(gen)];
            const std::string code(entry.code);

            // Build via the catalog-backed factory and randomize the variable
            // parts so the assertion holds across many shapes.
            Error error = Error::fromCode(code, randomRequestId(gen));
            error.message = randomMessage(gen, 1, 120);
            error.details = (detailDist(gen) == 1) ? randomMessage(gen, 1, 80) : std::string();

            // The code is registered -> hasNumericCode() is true and numericCode()
            // equals the catalog entry's numeric code.
            if (!error.hasNumericCode() || error.numericCode() != entry.numericCode)
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] PRESENT branch: code=" << code
                          << " hasNumericCode=" << error.hasNumericCode()
                          << " numericCode=" << error.numericCode()
                          << " expected=" << entry.numericCode;
            }
            REQUIRE(error.hasNumericCode());
            CHECK(error.numericCode() == entry.numericCode);

            // The Envelope must contain `numeric_code` equal to the registered value.
            const Json::Value root = error.toJson(includeDetails);
            REQUIRE(root.isObject());
            REQUIRE(root.isMember("error"));
            const Json::Value &err = root["error"];

            if (
              !err.isMember("numeric_code") || !err["numeric_code"].isInt() ||
              err["numeric_code"].asInt() != entry.numericCode
            )
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] PRESENT branch: code=" << code
                          << " present=" << err.isMember("numeric_code")
                          << " value=" << err["numeric_code"].asInt()
                          << " expected=" << entry.numericCode;
            }
            REQUIRE(err.isMember("numeric_code"));
            CHECK(err["numeric_code"].isInt());
            CHECK(err["numeric_code"].asInt() == entry.numericCode);
        }
        else
        {
            // ---- OMISSION branch: an unregistered code omits numeric_code ----
            // Built DIRECTLY via the Error struct (NOT fromCode, which would
            // remap to INTERNAL_ERROR and wrongly attach numeric 6001).
            const std::string code = kUnregisteredCodes[unregDist(gen)];

            Error error;
            error.code = code;
            error.category = kCategories[catDist(gen)];
            error.message = randomMessage(gen, 1, 120);
            error.details = (detailDist(gen) == 1) ? randomMessage(gen, 1, 80) : std::string();
            error.requestId = randomRequestId(gen);

            // Unregistered -> hasNumericCode() is false.
            if (error.hasNumericCode())
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] OMISSION branch: code=" << code
                          << " unexpectedly reports hasNumericCode()=true";
            }
            REQUIRE(!error.hasNumericCode());

            // The Envelope must COMPLETELY omit the `numeric_code` key: the member
            // must be absent (not null, not an empty string).
            const Json::Value root = error.toJson(includeDetails);
            REQUIRE(root.isObject());
            REQUIRE(root.isMember("error"));
            const Json::Value &err = root["error"];

            if (err.isMember("numeric_code"))
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] OMISSION branch: code=" << code
                          << " unexpectedly has numeric_code key (isNull="
                          << err["numeric_code"].isNull() << ")";
            }
            CHECK(!err.isMember("numeric_code"));
        }
    }
}

// Feature: error-code-message-standardization, Property 6: 生产模式安全隔离
//
// Property 6: 生产模式安全隔离  (task 4.5)
//
// 对任意 Error_Code 或异常（包括其文本含 SQL 语句、数据库驱动错误、文件系统路径或
// 堆栈样式片段的情形），当后端运行于 Production_Mode 时产生的 Envelope：
//   (a) 完全不含 `details` 键（既非 null 也非空串，而是该成员根本不存在）；
//   (b) `message` 字段取值等于该 code 在 Error_Catalog 中登记的默认 Client_Safe_Message；
//   (c) 所有返回给客户端的字符串字段（`message` 及任何其他存在的字段）均不含任一被注入的
//       敏感片段（SQL 语句、数据库驱动错误文本、文件系统路径、堆栈跟踪）。
//
// Validates: Requirements 5.1, 5.3, 5.6, 12.4
//
// 如何驱动 Production_Mode：经 ErrorContext::setDetailedErrorsOverride(false) 强制
// 进入 Production_Mode（即 detailedErrorsAllowed() == false），与构建标志/环境变量无关。
// Requirement 12.4 要求该安全断言「在开发、预发布、生产配置下均执行」——由于本测试以
// setDetailedErrorsOverride(false) 确定性地强制 Production_Mode，断言本身与运行配置无关：
// 无论宿主在何种构建/环境下运行，被验证的都是 Production_Mode 的安全保证。
// （非生产诊断信息分支由 Property 7 / 任务 4.6 覆盖。）
//
// 两种被测来源，覆盖「任意 Error_Code 或异常」：
//   * fromCode 分支：取任一登记 code，经 Error::fromCode 构造（message 即 Catalog 默认
//     Client_Safe_Message），随后把恶意文本塞进 error.details，模拟统一入口收到的
//     clientDetails/detailForLog。Production_Mode 下 details 被省略，message 保持默认值。
//   * fromException 分支：以 what() 含恶意文本的 std::exception 经 Error::fromException
//     构造（fromException 把 e.what() 记为 details、message 取映射 code 的 Catalog 默认值，
//     映射结果为类别对应的登记 code 或 INTERNAL 兜底）。Production_Mode 下 details 被省略。
//
// 每次迭代都同时经由两条产出路径生成 Envelope 并断言：
//   (1) error.toJson(ErrorContext::detailedErrorsAllowed())，即 includeDetails=false；
//   (2) ErrorResponder::buildResponse(req, error)（解析其响应体），其内部同样依据
//       ErrorContext 判定 includeDetails。
// 两条路径都断言无 `details` 键、message 等于 Catalog 默认值、所有字符串字段不含任一敏感
// 片段。Catalog 通过 allEntries() 泛型遍历，条目数量从不写死。测试结束时务必
// clearDetailedErrorsOverride()，保持文件对后续 4.6/4.7 追加属性可干净复用。
DROGON_TEST(Unit_P0_ErrorEnvelope_Property6_ProductionModeSafetyIsolation)
{
    // Fixed, printable seed so any failure is reproducible.
    constexpr unsigned int kSeed = 0x6'5AFE'0DEu;
    LOG_INFO << "Property 6 production-mode safety-isolation test, fixed seed=0x" << seedHex(kSeed);

    std::mt19937 gen(kSeed);

    const auto &entries = ErrorCatalog::allEntries();
    REQUIRE(!entries.empty());

    // Malicious fragments that MUST never reach a client string field in
    // Production_Mode: SQL statements, DB driver errors, filesystem paths and
    // stack-trace-style snippets (Requirement 5.3). These are pure ASCII; all
    // catalog default Client_Safe_Messages are Chinese, so a legitimate default
    // can never accidentally contain one of these (no false positives).
    static const std::vector<std::string> kSensitiveFragments =
      {"SELECT * FROM users WHERE id=1",
       "DROP TABLE accounts",
       "DrogonDbException: duplicate key value violates unique constraint",
       "syntax error at or near \"FROM\"",
       "/etc/passwd",
       "/var/lib/postgresql/data/base/16384",
       "C:\\src\\oauth2\\foo.cc:42",
       "at 0xdeadbeef in libpq.so",
       "Traceback (most recent call last)",
       "java.lang.NullPointerException at com.example.Foo.bar(Foo.java:42)"};

    // Category hints fed to Error::fromException (every ErrorCategory value).
    static const std::vector<ErrorCategory> kCategories =
      {ErrorCategory::NETWORK,
       ErrorCategory::DATABASE,
       ErrorCategory::VALIDATION,
       ErrorCategory::AUTHENTICATION,
       ErrorCategory::AUTHORIZATION,
       ErrorCategory::INTERNAL,
       ErrorCategory::UNKNOWN};

    // Force Production_Mode for the entire loop. Restored at the very end so the
    // following property tests (4.6 / 4.7) observe the default error context.
    ErrorContext::setDetailedErrorsOverride(false);
    REQUIRE(!ErrorContext::detailedErrorsAllowed());  // confirm Production_Mode is in effect.

    std::uniform_int_distribution<std::size_t> entryDist(0, entries.size() - 1);
    std::uniform_int_distribution<std::size_t> catDist(0, kCategories.size() - 1);
    std::uniform_int_distribution<int> branchDist(0, 1);
    std::uniform_int_distribution<int> coin(0, 1);

    constexpr int kIterations = 200;  // >= 100 per the PBT convention.
    for (int i = 0; i < kIterations; ++i)
    {
        // Build a malicious payload from a random non-empty subset of the
        // sensitive fragments interleaved with random UTF-8 noise, so each
        // iteration injects a different blend.
        std::string malicious;
        for (std::size_t k = 0; k < kSensitiveFragments.size(); ++k)
        {
            if (coin(gen) == 1)
            {
                malicious += kSensitiveFragments[k];
                malicious += " | ";
            }
        }
        if (malicious.empty())  // guarantee at least one fragment is present.
        {
            malicious = kSensitiveFragments[0];
        }
        malicious += randomMessage(gen, 0, 20);  // trailing noise.

        // Construct the Error exactly as the production entry points would, then
        // record the expected (catalog default) message for the resulting code.
        Error error;
        std::string expectedMessage;
        std::string sourceCode;  // for diagnostics only.

        if (branchDist(gen) == 0)
        {
            // ---- fromCode branch: any registered code, malicious `details` ----
            const CatalogEntry &entry = entries[entryDist(gen)];
            sourceCode = std::string(entry.code);

            // fromCode sets message to the catalog default Client_Safe_Message.
            error = Error::fromCode(sourceCode, randomRequestId(gen));
            // Inject malicious text into the diagnostic detail (client-supplied /
            // log detail). In Production_Mode this key must be omitted entirely.
            error.details = malicious;

            // The code is registered -> the expected message is its default.
            expectedMessage = std::string(entry.defaultMessage);
        }
        else
        {
            // ---- fromException branch: malicious what() captured as details ---
            const ErrorCategory hint = kCategories[catDist(gen)];
            const std::runtime_error ex(malicious);  // what() == malicious text.

            error = Error::fromException(ex, hint, randomRequestId(gen));
            sourceCode = error.code;

            // fromException maps to a registered code (category-mapped or the
            // INTERNAL fallback) whose default message is the Client_Safe_Message.
            const CatalogEntry *mapped = ErrorCatalog::find(error.code);
            if (mapped == nullptr)
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] fromException produced UNREGISTERED code='" << error.code
                          << "' (hint category=" << toString(hint) << ")";
            }
            REQUIRE(mapped != nullptr);
            expectedMessage = std::string(mapped->defaultMessage);

            // fromException must capture the raw text as Internal_Detail so that
            // the omission guarantee below is actually meaningful (the malicious
            // text really exists in `details` and is only hidden by Production_Mode).
            CHECK(error.details == malicious);
        }

        // Produce the Envelope via BOTH paths. Both consult ErrorContext, which
        // is pinned to Production_Mode (detailedErrorsAllowed() == false).
        const Json::Value toJsonRoot = error.toJson(ErrorContext::detailedErrorsAllowed());

        const ::drogon::HttpRequestPtr req = ::drogon::HttpRequest::newHttpRequest();
        const ::drogon::HttpResponsePtr resp = ErrorResponder::buildResponse(req, error);
        REQUIRE(resp != nullptr);
        Json::Value respRoot;
        const bool parsed = parseJson(std::string(resp->getBody()), respRoot);
        if (!parsed)
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                      << "] buildResponse body is not parseable JSON for code=" << sourceCode;
        }
        REQUIRE(parsed);

        // Run the identical Production_Mode safety assertions against the `error`
        // object produced by each path.
        REQUIRE(toJsonRoot.isObject());
        REQUIRE(toJsonRoot.isMember("error"));
        REQUIRE(toJsonRoot["error"].isObject());
        REQUIRE(respRoot.isObject());
        REQUIRE(respRoot.isMember("error"));
        REQUIRE(respRoot["error"].isObject());

        const std::pair<const char *, const Json::Value *> envelopes[] =
          {{"toJson", &toJsonRoot["error"]}, {"buildResponse", &respRoot["error"]}};

        for (const auto &env : envelopes)
        {
            const char *which = env.first;
            const Json::Value &err = *env.second;

            // (a) Production_Mode: `details` is fully omitted (Requirement 5.1).
            if (err.isMember("details"))
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << " via=" << which << "] code=" << sourceCode
                          << " unexpectedly carries `details` in Production_Mode";
            }
            CHECK(!err.isMember("details"));

            // (b) `message` equals the catalog default Client_Safe_Message for the
            //     code (Requirement 5.6), i.e. NOT the raw exception / DB text.
            REQUIRE(err.isMember("message"));
            REQUIRE(err["message"].isString());
            if (err["message"].asString() != expectedMessage)
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << " via=" << which << "] code=" << sourceCode
                          << " message mismatch: expected catalog default='" << expectedMessage
                          << "' got='" << err["message"].asString() << "'";
            }
            CHECK(err["message"].asString() == expectedMessage);

            // (c) NO returned string field contains any injected sensitive
            //     fragment (Requirement 5.3 / 12.4). Iterate EVERY string member
            //     and check EVERY fragment (stronger than only those injected).
            for (const auto &key : err.getMemberNames())
            {
                if (!err[key].isString())
                {
                    continue;
                }
                const std::string value = err[key].asString();
                for (const auto &frag : kSensitiveFragments)
                {
                    if (value.find(frag) != std::string::npos)
                    {
                        LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                                  << " via=" << which << "] code=" << sourceCode << " field '"
                                  << key << "' leaked sensitive fragment '" << frag << "' (value='"
                                  << value << "')";
                    }
                    CHECK(value.find(frag) == std::string::npos);
                }
            }
        }
    }

    // Restore the default error context for subsequent property tests.
    ErrorContext::clearDetailedErrorsOverride();
}

// Feature: error-code-message-standardization, Property 7: 非生产模式诊断信息
//
// Property 7: 非生产模式诊断信息  (task 4.6)
//
// 对任意 Error，当后端运行于非 Production_Mode（或显式开启详细错误开关，即
// detailedErrorsAllowed() == true）时产生的 Envelope：
//   (a) 通用诊断分支：`details` 键 PRESENT，且为非空字符串，并包含本次设置/捕获到的
//       诊断文本（fromCode 注入的 clientDetails，或 fromException 捕获的 e.what()）
//       —— 即 Requirement 5.4「非生产模式下 details 含附加诊断信息」。
//   (b) VALIDATION 转换分支：由字段级校验失败转换得到的 VALIDATION Envelope，其
//       `code` == VALIDATION_INVALID_INPUT、`category` == VALIDATION（HTTP 400），且
//       `details` PRESENT 并包含每一个触发失败的字段名称与每一条对应失败原因
//       —— 即 Requirement 7.6。
//
// Validates: Requirements 5.4, 7.6
//
// 如何驱动非 Production_Mode：经 ErrorContext::setDetailedErrorsOverride(true) 强制
// detailedErrorsAllowed() == true，与构建标志/环境变量无关；这是 Property 6 的对偶。
//
// (a) 通用诊断分支随机在两条产出来源间切换，覆盖「任意 Error」：
//   * fromCode 分支：取任一登记 code，经 Error::fromCode 构造，随后把随机诊断文本写入
//     error.details（模拟统一入口收到的 clientDetails）。
//   * fromException 分支：以 what() 含随机文本的 std::runtime_error 经
//     Error::fromException 构造（fromException 把 e.what() 记为 details）。
//   每条都同时经 error.toJson(ErrorContext::detailedErrorsAllowed())（includeDetails
//   == true）与 ErrorResponder::buildResponse(req, error)（解析其响应体，其内部同样依据
//   ErrorContext 判定 includeDetails）产出 Envelope，断言 `details` 存在、为非空字符串
//   且包含注入的诊断文本。
//
// (b) VALIDATION 转换分支构造一个随机的非空 std::vector<FieldError>（随机字段名 +
//   失败原因），经 ErrorResponder::respondValidation 以同步回调捕获响应、解析响应体，
//   断言 code/category 取值并断言 details 含每个字段名与每条原因子串。
//
// Catalog 通过 allEntries() 泛型遍历，条目数量从不写死。测试结束时务必
// clearDetailedErrorsOverride()，保持文件对后续 4.7 追加属性可干净复用。
DROGON_TEST(Unit_P0_ErrorEnvelope_Property7_NonProductionDiagnosticDetails)
{
    // Fixed, printable seed so any failure is reproducible.
    constexpr unsigned int kSeed = 0x7DE7'A0DEu;
    LOG_INFO << "Property 7 non-production diagnostic-details test, fixed seed=0x"
             << seedHex(kSeed);

    std::mt19937 gen(kSeed);

    const auto &entries = ErrorCatalog::allEntries();
    REQUIRE(!entries.empty());

    // Category hints fed to Error::fromException (every ErrorCategory value).
    static const std::vector<ErrorCategory> kCategories =
      {ErrorCategory::NETWORK,
       ErrorCategory::DATABASE,
       ErrorCategory::VALIDATION,
       ErrorCategory::AUTHENTICATION,
       ErrorCategory::AUTHORIZATION,
       ErrorCategory::INTERNAL,
       ErrorCategory::UNKNOWN};

    // Force non-Production_Mode for the entire loop (the dual of Property 6).
    // Restored at the very end so the following property test (4.7) observes the
    // default error context.
    ErrorContext::setDetailedErrorsOverride(true);
    REQUIRE(ErrorContext::detailedErrorsAllowed());  // confirm non-Production_Mode is in effect.

    std::uniform_int_distribution<std::size_t> entryDist(0, entries.size() - 1);
    std::uniform_int_distribution<std::size_t> catDist(0, kCategories.size() - 1);
    std::uniform_int_distribution<int> branchDist(0, 1);
    std::uniform_int_distribution<std::size_t> fieldCountDist(1, 5);

    constexpr int kIterations = 200;  // >= 100 per the PBT convention.
    for (int i = 0; i < kIterations; ++i)
    {
        // ====================================================================
        // (a) GENERAL DIAGNOSTIC BRANCH — `details` present + carries the text.
        // ====================================================================
        {
            // The diagnostic text the Envelope is expected to surface. Non-empty
            // (minLen 1) so "details is non-empty" is a meaningful assertion and
            // the substring check below is not trivially satisfied.
            const std::string diagnosticText = randomMessage(gen, 1, 80);

            Error error;
            std::string sourceCode;  // for diagnostics only.

            if (branchDist(gen) == 0)
            {
                // ---- fromCode branch: diagnostic text as client `details`. ----
                const CatalogEntry &entry = entries[entryDist(gen)];
                sourceCode = std::string(entry.code);
                error = Error::fromCode(sourceCode, randomRequestId(gen));
                error.details = diagnosticText;
            }
            else
            {
                // ---- fromException branch: what() captured as `details`. ------
                const ErrorCategory hint = kCategories[catDist(gen)];
                const std::runtime_error ex(diagnosticText);  // what() == diagnosticText.
                error = Error::fromException(ex, hint, randomRequestId(gen));
                sourceCode = error.code;

                // fromException must capture the raw text as Internal_Detail so the
                // presence assertion below is meaningful.
                if (error.details != diagnosticText)
                {
                    LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                              << "] fromException did not capture what() as details: code="
                              << sourceCode << " details='" << error.details << "' expected='"
                              << diagnosticText << "'";
                }
                REQUIRE(error.details == diagnosticText);
            }

            // Produce the Envelope via BOTH paths. Both consult ErrorContext,
            // which is pinned to non-Production_Mode (detailedErrorsAllowed()).
            const Json::Value toJsonRoot = error.toJson(ErrorContext::detailedErrorsAllowed());

            const ::drogon::HttpRequestPtr req = ::drogon::HttpRequest::newHttpRequest();
            const ::drogon::HttpResponsePtr resp = ErrorResponder::buildResponse(req, error);
            REQUIRE(resp != nullptr);
            Json::Value respRoot;
            const bool parsed = parseJson(std::string(resp->getBody()), respRoot);
            if (!parsed)
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] buildResponse body is not parseable JSON for code=" << sourceCode;
            }
            REQUIRE(parsed);

            REQUIRE(toJsonRoot.isObject());
            REQUIRE(toJsonRoot.isMember("error"));
            REQUIRE(toJsonRoot["error"].isObject());
            REQUIRE(respRoot.isObject());
            REQUIRE(respRoot.isMember("error"));
            REQUIRE(respRoot["error"].isObject());

            const std::pair<const char *, const Json::Value *> envelopes[] =
              {{"toJson", &toJsonRoot["error"]}, {"buildResponse", &respRoot["error"]}};

            for (const auto &env : envelopes)
            {
                const char *which = env.first;
                const Json::Value &err = *env.second;

                // (a) Non-Production_Mode: `details` is PRESENT (Requirement 5.4).
                if (!err.isMember("details"))
                {
                    LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                              << " via=" << which << "] code=" << sourceCode
                              << " is missing `details` in non-Production_Mode";
                }
                REQUIRE(err.isMember("details"));
                REQUIRE(err["details"].isString());

                // ... and is a non-empty string that contains the diagnostic text.
                const std::string detailsValue = err["details"].asString();
                if (detailsValue.empty() || detailsValue.find(diagnosticText) == std::string::npos)
                {
                    LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                              << " via=" << which << "] code=" << sourceCode
                              << " details does not carry diagnostic text: details='"
                              << detailsValue << "' expected to contain='" << diagnosticText << "'";
                }
                CHECK(!detailsValue.empty());
                CHECK(detailsValue.find(diagnosticText) != std::string::npos);
            }
        }

        // ====================================================================
        // (b) VALIDATION CONVERSION BRANCH — details lists every field+reason.
        // ====================================================================
        {
            // Build a random, non-empty set of field-level failures. Field names
            // and reasons are non-empty so the substring assertions are meaningful.
            const std::size_t count = fieldCountDist(gen);
            std::vector<FieldError> fieldErrors;
            fieldErrors.reserve(count);
            for (std::size_t k = 0; k < count; ++k)
            {
                FieldError fe;
                fe.field = randomMessage(gen, 1, 24);
                fe.reason = randomMessage(gen, 1, 48);
                fieldErrors.push_back(std::move(fe));
            }

            // Drive the unified VALIDATION entry point with a synchronous callback
            // that captures the response (Drogon's callback is invoked inline here).
            const ::drogon::HttpRequestPtr req = ::drogon::HttpRequest::newHttpRequest();
            ::drogon::HttpResponsePtr captured;
            ErrorResponder::respondValidation(
              req, [&captured](const ::drogon::HttpResponsePtr &r) { captured = r; }, fieldErrors
            );
            REQUIRE(captured != nullptr);

            Json::Value respRoot;
            const bool parsed = parseJson(std::string(captured->getBody()), respRoot);
            if (!parsed)
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] respondValidation body is not parseable JSON";
            }
            REQUIRE(parsed);
            REQUIRE(respRoot.isObject());
            REQUIRE(respRoot.isMember("error"));
            REQUIRE(respRoot["error"].isObject());
            const Json::Value &err = respRoot["error"];

            // code == VALIDATION_INVALID_INPUT (Requirement 7.4).
            REQUIRE(err.isMember("code"));
            REQUIRE(err["code"].isString());
            if (err["code"].asString() != "VALIDATION_INVALID_INPUT")
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] validation code mismatch: got='" << err["code"].asString()
                          << "' expected='VALIDATION_INVALID_INPUT'";
            }
            CHECK(err["code"].asString() == "VALIDATION_INVALID_INPUT");

            // category == VALIDATION (Requirement 7.4).
            REQUIRE(err.isMember("category"));
            REQUIRE(err["category"].isString());
            const std::string expectedCategory = toString(ErrorCategory::VALIDATION);
            if (err["category"].asString() != expectedCategory)
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] validation category mismatch: got='" << err["category"].asString()
                          << "' expected='" << expectedCategory << "'";
            }
            CHECK(err["category"].asString() == expectedCategory);

            // HTTP 400 for VALIDATION (Requirement 7.4).
            if (captured->statusCode() != ::drogon::k400BadRequest)
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] validation HTTP status mismatch: got="
                          << static_cast<int>(captured->statusCode()) << " expected=400";
            }
            CHECK(captured->statusCode() == ::drogon::k400BadRequest);

            // details PRESENT and contains EACH field name and EACH reason
            // (Requirement 7.6).
            if (!err.isMember("details"))
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] validation Envelope missing `details` in non-Production_Mode";
            }
            REQUIRE(err.isMember("details"));
            REQUIRE(err["details"].isString());
            const std::string detailsValue = err["details"].asString();
            CHECK(!detailsValue.empty());

            for (const auto &fe : fieldErrors)
            {
                if (detailsValue.find(fe.field) == std::string::npos)
                {
                    LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                              << "] validation details missing field name '" << fe.field
                              << "': details='" << detailsValue << "'";
                }
                CHECK(detailsValue.find(fe.field) != std::string::npos);

                if (detailsValue.find(fe.reason) == std::string::npos)
                {
                    LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                              << "] validation details missing reason '" << fe.reason
                              << "': details='" << detailsValue << "'";
                }
                CHECK(detailsValue.find(fe.reason) != std::string::npos);
            }
        }
    }

    // Restore the default error context for subsequent property tests.
    ErrorContext::clearDetailedErrorsOverride();
}

// Feature: error-code-message-standardization, Property 8: 未登记异常的内部错误兜底
//
// Property 8: 未登记异常的内部错误兜底  (task 4.7)
//
// 对任意未在 Error_Catalog 中登记映射的异常，统一入口返回的 Envelope 满足：
//   (a) category == INTERNAL；
//   (b) code 为内部错误码 INTERNAL_ERROR（其 Numeric_Error_Code 为 6001）；
//   (c) message 等于该 code 在 Error_Catalog 中登记的默认 Client_Safe_Message
//       （即 ErrorCatalog::internalError().defaultMessage），而非异常原始文本。
//
// Validates: Requirements 5.5
//
// 何为"未登记/未映射的异常"：异常 -> Error_Code 的映射完全由 category 提示驱动
// （ErrorHandler.cc 内的 representativeCodeFor）。当提示为 UNKNOWN 或 INTERNAL 时，
// 不存在与之对应的某个具体业务码，实现会兜底到 ErrorCatalog::internalError()
// （INTERNAL_ERROR / numeric 6001 / category INTERNAL）。具体类别（DATABASE/NETWORK/
// VALIDATION/AUTHENTICATION/AUTHORIZATION）则会映射到各自已登记的代表码——那是"已映射"
// 情形，不在本属性范围内。因此本属性以 ErrorCategory::UNKNOWN 与 ErrorCategory::INTERNAL
// 作为"未映射"提示来确定性地驱动兜底路径（Requirement 5.5）。
//
// 为覆盖"任意未登记异常"，每次迭代从多种 std::exception 子类（含一个自定义子类）中随机
// 选择一种构造，其 what() 为随机文本（含 ASCII / 非 ASCII / JSON 敏感字符），并随机选用
// UNKNOWN 或 INTERNAL 提示。每次迭代同时经两条产出路径生成 Envelope 并断言相同的兜底
// 不变量：
//   (1) Error::fromException(ex, hint, requestId)（直接工厂）；
//   (2) ErrorResponder::respondException(req, cb, ex, hint)（统一入口，解析其响应体）。
// includeDetails / Production_Mode 在 true/false 间切换，证明兜底的 category/code/message
// 与 details 是否出现无关。兜底数值经 ErrorCatalog::internalError() 读取，从不写死。
// 本属性以 ErrorContext override 切换生产/非生产模式，结束时清除 override 保持文件干净。
DROGON_TEST(Unit_P0_ErrorEnvelope_Property8_UnmappedExceptionInternalFallback)
{
    // Fixed, printable seed so any failure is reproducible.
    constexpr unsigned int kSeed = 0x8'F00'0DEu;
    LOG_INFO << "Property 8 unmapped-exception internal-fallback test, fixed seed=0x"
             << seedHex(kSeed);

    std::mt19937 gen(kSeed);

    // The expected fallback entry: INTERNAL_ERROR / numeric 6001 / category INTERNAL /
    // default Client_Safe_Message. Read from the catalog (numerics are never
    // hard-coded as separate magic constants — they are asserted against the
    // single source of truth here).
    const CatalogEntry &internal = ErrorCatalog::internalError();
    const std::string expectedCode(internal.code);
    const std::string expectedMessage(internal.defaultMessage);
    REQUIRE(expectedCode == "INTERNAL_ERROR");
    REQUIRE(internal.numericCode == 6001);
    REQUIRE(internal.category == ErrorCategory::INTERNAL);

    // A custom std::exception subclass whose what() is arbitrary runtime text,
    // standing in for an application-specific exception that has NO catalog
    // mapping of its own. (Local class: confined to this property test.)
    struct CustomException : std::exception
    {
        std::string text;

        explicit CustomException(std::string t) : text(std::move(t))
        {
        }

        const char *what() const noexcept override
        {
            return text.c_str();
        }
    };

    // "Unmapped" category hints: neither corresponds to a specific business code,
    // so Error::fromException falls back to INTERNAL_ERROR (Requirement 5.5).
    static const std::vector<ErrorCategory> kUnmappedHints =
      {ErrorCategory::UNKNOWN, ErrorCategory::INTERNAL};

    std::uniform_int_distribution<std::size_t> hintDist(0, kUnmappedHints.size() - 1);
    std::uniform_int_distribution<int> kindDist(0, 4);  // exception subclass selector.
    std::uniform_int_distribution<int> coin(0, 1);      // includeDetails / Production_Mode toggle.

    // Shared assertion routine: build the Envelope via BOTH the direct factory
    // (Error::fromException) and the unified entry point
    // (ErrorResponder::respondException), then assert the fallback invariants on
    // each `error` object. ErrorContext is pinned to `includeDetails` so both
    // paths agree on the Production_Mode decision.
    auto runChecks =
      [&](const std::exception &ex, ErrorCategory hint, bool includeDetails, int iter) {
          ErrorContext::setDetailedErrorsOverride(includeDetails);

          // ---- (1) Direct factory path -----------------------------------------
          const Error error = Error::fromException(ex, hint, randomRequestId(gen));

          // The mapped code is the INTERNAL fallback regardless of exception text.
          if (error.code != expectedCode || error.category != ErrorCategory::INTERNAL)
          {
              LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << iter
                        << " hint=" << toString(hint) << "] fromException did not fall back to "
                        << expectedCode << ": code='" << error.code
                        << "' category=" << toString(error.category) << " what='" << ex.what()
                        << "'";
          }
          REQUIRE(error.code == expectedCode);
          CHECK(error.category == ErrorCategory::INTERNAL);
          REQUIRE(error.hasNumericCode());
          CHECK(error.numericCode() == 6001);
          CHECK(error.message == expectedMessage);

          const Json::Value toJsonRoot = error.toJson(includeDetails);

          // ---- (2) Unified entry point path ------------------------------------
          // respondException invokes the callback synchronously; capture the response.
          const ::drogon::HttpRequestPtr req = ::drogon::HttpRequest::newHttpRequest();
          ::drogon::HttpResponsePtr captured;
          ErrorResponder::respondException(
            req, [&captured](const ::drogon::HttpResponsePtr &r) { captured = r; }, ex, hint
          );
          REQUIRE(captured != nullptr);

          Json::Value respRoot;
          const bool parsed = parseJson(std::string(captured->getBody()), respRoot);
          if (!parsed)
          {
              LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << iter
                        << "] respondException body is not parseable JSON; what='" << ex.what()
                        << "'";
          }
          REQUIRE(parsed);

          REQUIRE(toJsonRoot.isObject());
          REQUIRE(toJsonRoot.isMember("error"));
          REQUIRE(toJsonRoot["error"].isObject());
          REQUIRE(respRoot.isObject());
          REQUIRE(respRoot.isMember("error"));
          REQUIRE(respRoot["error"].isObject());

          const std::pair<const char *, const Json::Value *> envelopes[] =
            {{"toJson", &toJsonRoot["error"]}, {"respondException", &respRoot["error"]}};

          for (const auto &env : envelopes)
          {
              const char *which = env.first;
              const Json::Value &err = *env.second;

              // (a) category == INTERNAL.
              REQUIRE(err.isMember("category"));
              REQUIRE(err["category"].isString());
              if (err["category"].asString() != "INTERNAL")
              {
                  LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << iter
                            << " via=" << which << "] category mismatch: got='"
                            << err["category"].asString() << "' expected='INTERNAL' what='"
                            << ex.what() << "'";
              }
              CHECK(err["category"].asString() == "INTERNAL");

              // (b) code == INTERNAL_ERROR with numeric_code == 6001.
              REQUIRE(err.isMember("code"));
              REQUIRE(err["code"].isString());
              if (err["code"].asString() != expectedCode)
              {
                  LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << iter
                            << " via=" << which << "] code mismatch: got='"
                            << err["code"].asString() << "' expected='" << expectedCode << "'";
              }
              CHECK(err["code"].asString() == expectedCode);

              REQUIRE(err.isMember("numeric_code"));
              REQUIRE(err["numeric_code"].isInt());
              if (err["numeric_code"].asInt() != 6001)
              {
                  LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << iter
                            << " via=" << which
                            << "] numeric_code mismatch: got=" << err["numeric_code"].asInt()
                            << " expected=6001";
              }
              CHECK(err["numeric_code"].asInt() == 6001);

              // (c) message == catalog default Client_Safe_Message for INTERNAL_ERROR
              //     (NOT the raw exception text).
              REQUIRE(err.isMember("message"));
              REQUIRE(err["message"].isString());
              if (err["message"].asString() != expectedMessage)
              {
                  LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << iter
                            << " via=" << which << "] message mismatch: got='"
                            << err["message"].asString() << "' expected catalog default='"
                            << expectedMessage << "'";
              }
              CHECK(err["message"].asString() == expectedMessage);
          }
      };

    constexpr int kIterations = 200;  // >= 100 per the PBT convention.
    for (int i = 0; i < kIterations; ++i)
    {
        const ErrorCategory hint = kUnmappedHints[hintDist(gen)];
        const bool includeDetails = (coin(gen) == 1);
        // what() text: random ASCII / non-ASCII / JSON-significant content. The
        // fallback must hold regardless of the exception text.
        const std::string what = randomMessage(gen, 1, 80);

        // Randomly select a std::exception subclass so "any unmapped exception"
        // is covered across the loop. Each exception lives on the stack for the
        // duration of runChecks (passed by const ref).
        switch (kindDist(gen))
        {
            case 0:
            {
                const std::runtime_error ex(what);
                runChecks(ex, hint, includeDetails, i);
                break;
            }
            case 1:
            {
                const std::logic_error ex(what);
                runChecks(ex, hint, includeDetails, i);
                break;
            }
            case 2:
            {
                const std::invalid_argument ex(what);
                runChecks(ex, hint, includeDetails, i);
                break;
            }
            case 3:
            {
                const std::out_of_range ex(what);
                runChecks(ex, hint, includeDetails, i);
                break;
            }
            default:
            {
                const CustomException ex(what);
                runChecks(ex, hint, includeDetails, i);
                break;
            }
        }
    }

    // Restore the default error context for any subsequent property tests.
    ErrorContext::clearDetailedErrorsOverride();
}
