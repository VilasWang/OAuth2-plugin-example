#include <drogon/drogon_test.h>
#include <authforge/common/error/ErrorCatalog.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace authforge::common::error;

// Feature: error-code-message-standardization
// Documentation generator/validator (Task 11.1) guarding Requirements 3.5, 3.7
// and 12.1: the committed "通用错误码" section of docs/backend/api-reference.md is
// the published Error_Catalog and MUST stay consistent with the single source of
// truth (ErrorCatalog::allEntries()/allOAuthEntries()). This validator parses the
// section's two tables and asserts there is exactly one row per Application
// Error_Code and per OAuth2 protocol code, each carrying the catalog's HTTP status
// code and (for Application codes) Error_Category / numeric_code / default message.
// Any mismatch (missing, extra, or divergent row) fails the test, gating CI.

namespace
{

// Absolute path to the doc, injected by the test CMakeLists via a compile
// definition (OAUTH2_DOC_API_REFERENCE) so the validator can locate the markdown
// file regardless of the test executable's working directory.
#ifndef OAUTH2_DOC_API_REFERENCE
#define OAUTH2_DOC_API_REFERENCE ""
#endif

const char *categoryName(ErrorCategory category)
{
    switch (category)
    {
        case ErrorCategory::NETWORK:
            return "NETWORK";
        case ErrorCategory::DATABASE:
            return "DATABASE";
        case ErrorCategory::VALIDATION:
            return "VALIDATION";
        case ErrorCategory::AUTHENTICATION:
            return "AUTHENTICATION";
        case ErrorCategory::AUTHORIZATION:
            return "AUTHORIZATION";
        case ErrorCategory::INTERNAL:
            return "INTERNAL";
        case ErrorCategory::UNKNOWN:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

std::string trim(const std::string &s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (std::isspace(static_cast<unsigned char>(s[b])) != 0))
    {
        ++b;
    }
    while (e > b && (std::isspace(static_cast<unsigned char>(s[e - 1])) != 0))
    {
        --e;
    }
    return s.substr(b, e - b);
}

// Strip surrounding markdown code-span backticks (and any stray backticks) and trim.
std::string cleanCell(const std::string &cell)
{
    std::string out;
    out.reserve(cell.size());
    for (char c : cell)
    {
        if (c != '`')
        {
            out.push_back(c);
        }
    }
    return trim(out);
}

// Split a markdown table row "| a | b | c |" into its inner cells {a, b, c}.
// Leading/trailing empty cells produced by the bounding pipes are dropped.
std::vector<std::string> splitRow(const std::string &line)
{
    std::vector<std::string> cells;
    std::string cur;
    for (char c : line)
    {
        if (c == '|')
        {
            cells.push_back(cleanCell(cur));
            cur.clear();
        }
        else
        {
            cur.push_back(c);
        }
    }
    cells.push_back(cleanCell(cur));
    // Drop the empty leading cell (before the first pipe) and trailing cell
    // (after the last pipe).
    if (!cells.empty() && cells.front().empty())
    {
        cells.erase(cells.begin());
    }
    if (!cells.empty() && cells.back().empty())
    {
        cells.pop_back();
    }
    return cells;
}

// Parse an integer cell. Returns false if the cell is not a pure integer (used to
// distinguish data rows from header/separator rows by their HTTP-status column).
bool parseInt(const std::string &s, int &out)
{
    if (s.empty())
    {
        return false;
    }
    for (char c : s)
    {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0)
        {
            return false;
        }
    }
    out = std::stoi(s);
    return true;
}

std::string readFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::string();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct AppRow
{
    std::string code;
    int numericCode;
    std::string category;
    int httpStatus;
    std::string message;
};

struct OAuthRow
{
    std::string error;
    int httpStatus;
    std::string desc;
};

// Extract [beginAnchor, endAnchor) region by ASCII-only markdown anchors so the
// parser never depends on the execution charset of multibyte headings.
std::string sliceBetween(
  const std::string &doc,
  const std::string &beginAnchor,
  const std::string &endAnchor,
  size_t from = 0
)
{
    const size_t b = doc.find(beginAnchor, from);
    if (b == std::string::npos)
    {
        return std::string();
    }
    const size_t e = doc.find(endAnchor, b + beginAnchor.size());
    return doc.substr(b, (e == std::string::npos ? doc.size() : e) - b);
}

std::vector<std::string> tableLines(const std::string &region)
{
    std::vector<std::string> lines;
    std::istringstream ss(region);
    std::string line;
    while (std::getline(ss, line))
    {
        const std::string t = trim(line);
        if (!t.empty() && t.front() == '|')
        {
            lines.push_back(t);
        }
    }
    return lines;
}

}  // namespace

// --- Requirements 3.5, 3.7, 12.1: Application error-code table consistency. ----
DROGON_TEST(Unit_P0_ErrorCatalogDoc_ApplicationTableMatchesCatalog)
{
    const std::string docPath = OAUTH2_DOC_API_REFERENCE;
    REQUIRE(!docPath.empty());  // Compile definition must be set by CMake.

    const std::string doc = readFile(docPath);
    REQUIRE(!doc.empty());  // The published Error_Catalog doc must exist & be readable.

    // The Application table lives in subsection 5.1 (between "### 5.1" and "### 5.2").
    const std::string region = sliceBetween(doc, "### 5.1", "### 5.2");
    REQUIRE(!region.empty());

    // Parse data rows: 5 columns whose 2nd (numeric) and 4th (HTTP) cells are integers.
    std::vector<AppRow> rows;
    for (const auto &line : tableLines(region))
    {
        const std::vector<std::string> cells = splitRow(line);
        if (cells.size() != 5)
        {
            continue;  // header / malformed row
        }
        int numeric = 0;
        int http = 0;
        if (!parseInt(cells[1], numeric) || !parseInt(cells[3], http))
        {
            continue;  // header/separator row (non-integer status column)
        }
        rows.push_back(AppRow{cells[0], numeric, cells[2], http, cells[4]});
    }

    const auto &entries = ErrorCatalog::allEntries();

    // Exactly one documented row per catalog Error_Code (no missing / extra rows).
    CHECK(rows.size() == entries.size());

    // Every catalog entry appears exactly once with matching attributes.
    for (const auto &e : entries)
    {
        const std::string code(e.code);
        int matches = 0;
        for (const auto &r : rows)
        {
            if (r.code == code)
            {
                ++matches;
                CHECK(r.numericCode == e.numericCode);
                CHECK(r.category == std::string(categoryName(e.category)));
                CHECK(r.httpStatus == e.httpStatus);
                CHECK(r.message == std::string(e.defaultMessage));
            }
        }
        // Missing (0) or duplicated (>1) documentation row for this Error_Code fails.
        CHECK(matches == 1);
    }

    // Every documented row maps to a registered Error_Code (catches typos/extras).
    for (const auto &r : rows)
    {
        const CatalogEntry *entry = ErrorCatalog::find(r.code);
        CHECK(entry != nullptr);
    }
}

// --- Requirements 3.5, 3.7, 12.1: OAuth2 protocol error-code table consistency. -
DROGON_TEST(Unit_P0_ErrorCatalogDoc_OAuthTableMatchesCatalog)
{
    const std::string docPath = OAUTH2_DOC_API_REFERENCE;
    REQUIRE(!docPath.empty());

    const std::string doc = readFile(docPath);
    REQUIRE(!doc.empty());

    // The OAuth2 protocol table lives in subsection 5.2 (between "### 5.2" and "### 5.3").
    const std::string region = sliceBetween(doc, "### 5.2", "### 5.3");
    REQUIRE(!region.empty());

    // Parse data rows: 3 columns whose 2nd (HTTP) cell is an integer.
    std::vector<OAuthRow> rows;
    for (const auto &line : tableLines(region))
    {
        const std::vector<std::string> cells = splitRow(line);
        if (cells.size() != 3)
        {
            continue;
        }
        int http = 0;
        if (!parseInt(cells[1], http))
        {
            continue;  // header/separator row
        }
        rows.push_back(OAuthRow{cells[0], http, cells[2]});
    }

    const auto &entries = ErrorCatalog::allOAuthEntries();

    // Exactly one documented row per protocol error code (no missing / extra rows).
    CHECK(rows.size() == entries.size());

    for (const auto &e : entries)
    {
        const std::string error(e.error);
        int matches = 0;
        for (const auto &r : rows)
        {
            if (r.error == error)
            {
                ++matches;
                CHECK(r.httpStatus == e.httpStatus);
                CHECK(r.desc == std::string(e.defaultErrorDesc));
            }
        }
        CHECK(matches == 1);
    }

    for (const auto &r : rows)
    {
        const OAuthCatalogEntry *entry = ErrorCatalog::findOAuth(r.error);
        CHECK(entry != nullptr);
    }
}
