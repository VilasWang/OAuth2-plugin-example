#pragma once

// M3 Task 20 (authforge-sdk-refactor, design.md §6 "libs/drogon # Drogon
// 绑定 ... 插件(装配器)/controllers/filters/views"): relocated verbatim
// from OAuth2Plugin/include/oauth2/validation/Rules.h. Pure constants/
// structs, zero OAuth2Plugin dependency, so this is the lowest-risk first
// slice of the M3 Drogon-binding migration. Namespace is
// authforge::drogon::validation (design.md §6 namespace sync); the old
// oauth2::validation:: location becomes a compatibility shim (same
// pattern as libs/oauth2's JwkManager.h shim from Task 17 slice 10) so
// existing call sites keep compiling until the full M8 namespace sweep.

#include <string>
#include <functional>

namespace authforge::drogon::validation
{

// OAuth2 validation patterns and constants (unchanged from legacy)
inline const char *CLIENT_ID_PATTERN = "^[a-zA-Z0-9._-]{1,128}$";
inline const size_t CLIENT_ID_MIN_LEN = 1;
inline const size_t CLIENT_ID_MAX_LEN = 128;

inline const char *REDIRECT_URI_PATTERN = "^https?://[a-zA-Z0-9.-]+(:[0-9]+)?(/[^\\s]*)?$";
inline const size_t REDIRECT_URI_MIN_LEN = 10;
inline const size_t REDIRECT_URI_MAX_LEN = 2048;

inline const char *SCOPE_PATTERN = "^[a-zA-Z0-9: ]+$";
inline const size_t SCOPE_MIN_LEN = 1;
inline const size_t SCOPE_MAX_LEN = 256;

inline const char *TOKEN_PATTERN = "^[a-zA-Z0-9._-]+$";
inline const size_t TOKEN_MIN_LEN = 32;

inline const char *RESPONSE_TYPE_PATTERN = "^[a-zA-Z0-9_]+$";
inline const char *GRANT_TYPE_PATTERN = "^[a-zA-Z0-9_]+$";
inline const char *USERNAME_PATTERN = "^[a-zA-Z0-9_]{1,100}$";
inline const char *PASSWORD_PATTERN = "^[a-zA-Z0-9!@#$%^&*()_+]{8,200}$";

// W3C HTML5 email pattern (RFC 5322 compatible subset). Permissive by design:
// accepts plus-addressing (user+tag@), subdomains, long TLDs.
inline const char *EMAIL_PATTERN =
  "^[a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]+@[a-zA-Z0-9]"
  "(?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?"
  "(?:\\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$";
inline const size_t EMAIL_MAX_LEN = 254;  // RFC 5321 limit

enum class RuleType
{
    NotEmpty,
    LengthLimit,
    Regex,
    NumericRange,
    UrlFormat,
    EmailFormat
};

struct Rule
{
    std::string field;
    std::string source;  // "query", "body", "header"
    bool required;
    size_t minLength = 0;
    size_t maxLength = 0;
    std::string pattern;
    std::function<bool(const std::string &)> custom;
};

struct Result
{
    bool ok;
    std::string field;
    std::string message;

    static Result success();
    static Result failure(const std::string &field, const std::string &message);
};

}  // namespace authforge::drogon::validation
