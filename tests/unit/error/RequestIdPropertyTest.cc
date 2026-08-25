// Feature: error-code-message-standardization, Property 10: Request_ID 解析与生成
//
// Property 10 (design.md):
//   对任意入站请求：若其携带的关联 ID 请求头取值合法（非空、长度不超过 128、
//   仅由 ASCII 字母数字及 `-`、`_` 组成），则解析结果等于该头取值；若请求头缺失、
//   为空、超过 128 字符或包含约定字符集之外的字符，则解析结果是一个新生成的、
//   长度 1..128 的非空 Request_ID；并且连续多次生成所得的 Request_ID 互不相同。
//
// Validates: Requirements 6.1, 6.3, 6.4, 6.5
//
// Implementation notes:
//   * Hand-written random loop (>= 100 iterations) seeded with a fixed
//     std::mt19937 seed so failures are reproducible. The offending input and
//     seed are printed via LOG_ERROR before the failing CHECK fires.
//   * Headers are attached to an in-process HttpRequest via
//     drogon::HttpRequest::newHttpRequest() + addHeader("X-Request-ID", value),
//     and resolution is exercised through fulla::common::error::RequestId::resolve.

#include <drogon/drogon_test.h>
#include <drogon/HttpRequest.h>
#include <fulla/drogon/error/RequestId.h>

#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using fulla::common::error::RequestId;

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

// Fixed seed: reproducible across runs; printed on failure for replay.
constexpr unsigned int kSeed = 0x5EED'1D10u;

// The agreed Request_ID character set: ASCII alphanumerics plus '-' and '_'.
const std::string kValidChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// Out-of-set characters used to build invalid samples. Deliberately excludes
// CR/LF/NUL (which are illegal in HTTP header values and would not round-trip
// through the request object), while still covering spaces, punctuation and
// non-ASCII bytes as required by Requirement 6.5.
const std::vector<char> kInvalidChars =
  {' ',
   '\t',
   '!',
   '@',
   '#',
   '$',
   '%',
   '^',
   '&',
   '*',
   '(',
   ')',
   '+',
   '=',
   '.',
   ',',
   ';',
   ':',
   '/',
   '\\',
   '\'',
   '"',
   '<',
   '>',
   '?',
   '[',
   ']',
   '{',
   '}',
   '|',
   '~',
   '`',
   static_cast<char>(0x80),
   static_cast<char>(0xC3),
   static_cast<char>(0xA9),
   static_cast<char>(0xE4),
   static_cast<char>(0xBD),
   static_cast<char>(0xA0)};

// Render a (possibly non-printable) sample for diagnostics.
std::string escapeForLog(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    static const char *hex = "0123456789ABCDEF";
    for (const unsigned char c : s)
    {
        if (c >= 0x20 && c < 0x7F)
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out += "\\x";
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

std::string randomValid(std::mt19937 &gen, std::size_t len)
{
    std::uniform_int_distribution<std::size_t> pick(0, kValidChars.size() - 1);
    std::string s;
    s.reserve(len);
    for (std::size_t i = 0; i < len; ++i)
    {
        s.push_back(kValidChars[pick(gen)]);
    }
    return s;
}

drogon::HttpRequestPtr makeRequestWithHeader(const std::string &value)
{
    auto req = drogon::HttpRequest::newHttpRequest();
    req->addHeader("X-Request-ID", value);
    return req;
}

}  // namespace

// Main property test: random valid / invalid / missing headers + generation
// uniqueness, all in one hand-written loop (>= 100 iterations).
DROGON_TEST(Unit_P0_RequestId_Property10_ResolveAndGenerate)
{
    LOG_INFO << "Property 10 RequestId test, fixed seed=0x" << seedHex(kSeed);

    std::mt19937 gen(kSeed);
    std::uniform_int_distribution<int> categoryDist(0, 4);
    std::uniform_int_distribution<std::size_t> validLenDist(1, 128);
    std::uniform_int_distribution<std::size_t> longLenDist(129, 320);
    std::uniform_int_distribution<std::size_t> invCharCountDist(1, 8);

    // Collect every generated id (from invalid/missing resolutions and from
    // direct generate() calls) to assert mutual distinctness.
    std::set<std::string> generatedIds;
    std::size_t generatedCount = 0;

    constexpr int kIterations = 200;  // >= 100 as required
    for (int i = 0; i < kIterations; ++i)
    {
        const int category = categoryDist(gen);

        if (category == 0)
        {
            // VALID header -> resolve reuses the header value verbatim (Req 6.3).
            const std::string sample = randomValid(gen, validLenDist(gen));

            if (!RequestId::isValid(sample))
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] expected valid sample but isValid==false: " << escapeForLog(sample);
            }
            CHECK(RequestId::isValid(sample) == true);

            auto req = makeRequestWithHeader(sample);
            const std::string resolved = RequestId::resolve(req);
            if (resolved != sample)
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << "] valid header not reused. header=" << escapeForLog(sample)
                          << " resolved=" << escapeForLog(resolved);
            }
            CHECK(resolved == sample);
        }
        else
        {
            // One of the "must regenerate" categories (Req 6.4 / 6.5).
            std::string sample;
            bool headerPresent = true;
            const char *label = "";

            if (category == 1)
            {
                // Empty header value.
                sample = "";
                label = "empty";
            }
            else if (category == 2)
            {
                // Too long (only the length disqualifies it).
                sample = randomValid(gen, longLenDist(gen));
                label = "too-long";
            }
            else if (category == 3)
            {
                // Contains out-of-set characters (length itself is in range).
                sample = randomValid(gen, validLenDist(gen));
                const std::size_t injects = invCharCountDist(gen);
                std::uniform_int_distribution<std::size_t> badPick(0, kInvalidChars.size() - 1);
                for (std::size_t k = 0; k < injects; ++k)
                {
                    std::uniform_int_distribution<std::size_t> posPick(0, sample.size());
                    sample.insert(
                      sample.begin() + static_cast<std::ptrdiff_t>(posPick(gen)),
                      kInvalidChars[badPick(gen)]
                    );
                }
                label = "bad-chars";
            }
            else
            {
                // Missing header entirely.
                headerPresent = false;
                label = "missing";
            }

            // For present-but-invalid samples, the classifier must reject them
            // (Requirement 6.5). (Missing header has no sample to classify.)
            if (headerPresent)
            {
                if (RequestId::isValid(sample))
                {
                    LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                              << " cat=" << label << "] expected invalid sample but isValid==true: "
                              << escapeForLog(sample);
                }
                CHECK(RequestId::isValid(sample) == false);
            }

            auto req =
              headerPresent ? makeRequestWithHeader(sample) : drogon::HttpRequest::newHttpRequest();
            const std::string resolved = RequestId::resolve(req);

            // Resolution must be a freshly generated, valid id (Req 6.1 / 6.4).
            if (!RequestId::isValid(resolved))
            {
                LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                          << " cat=" << label
                          << "] resolved id is not valid: " << escapeForLog(resolved)
                          << " (sample=" << escapeForLog(sample) << ")";
            }
            CHECK(RequestId::isValid(resolved) == true);

            // Length bound 1..128 (Requirement 6.4).
            CHECK(!resolved.empty());
            CHECK(resolved.size() <= 128);

            // The invalid input must NOT be reused as the Request_ID.
            if (headerPresent)
            {
                if (resolved == sample)
                {
                    LOG_ERROR << "[seed=0x" << seedHex(kSeed) << " iter=" << i
                              << " cat=" << label
                              << "] invalid header was reused: " << escapeForLog(sample);
                }
                CHECK(resolved != sample);
            }

            generatedIds.insert(resolved);
            ++generatedCount;
        }
    }

    // Also exercise generate() directly to reinforce the uniqueness assertion
    // over consecutive calls (Requirement 6.4).
    for (int i = 0; i < 100; ++i)
    {
        const std::string id = RequestId::generate();
        if (!RequestId::isValid(id))
        {
            LOG_ERROR << "[seed=0x" << seedHex(kSeed)
                      << "] generate() produced invalid id: " << escapeForLog(id);
        }
        CHECK(RequestId::isValid(id) == true);
        generatedIds.insert(id);
        ++generatedCount;
    }

    // Every generated id must be mutually distinct: no collisions in the set.
    if (generatedIds.size() != generatedCount)
    {
        LOG_ERROR << "[seed=0x" << seedHex(kSeed)
                  << "] generated id collision: produced " << generatedCount << " ids but only "
                  << generatedIds.size() << " unique";
    }
    CHECK(generatedIds.size() == generatedCount);
}

// Focused example test for the exact length boundary (128 valid, 129 invalid).
DROGON_TEST(Unit_P0_RequestId_Property10_LengthBoundary)
{
    const std::string at128(128, 'a');
    const std::string at129(129, 'a');

    CHECK(RequestId::isValid(at128) == true);
    CHECK(RequestId::isValid(at129) == false);

    auto reqOk = drogon::HttpRequest::newHttpRequest();
    reqOk->addHeader("X-Request-ID", at128);
    CHECK(RequestId::resolve(reqOk) == at128);

    auto reqTooLong = drogon::HttpRequest::newHttpRequest();
    reqTooLong->addHeader("X-Request-ID", at129);
    const std::string resolved = RequestId::resolve(reqTooLong);
    CHECK(resolved != at129);
    CHECK(RequestId::isValid(resolved) == true);
    CHECK(resolved.size() <= 128);
}
