// Task 13 (authforge-sdk-refactor, design.md §4.1/§5.1/§6): framework-agnostic
// port of OAuth2Plugin/src/error/ErrorCatalog.cc. Data tables are copied
// verbatim (same codes/numeric values/messages -- Requirement-equivalent to
// the original "integer values preserved unchanged" rule); only the
// LOG_FATAL + std::abort() fail-fast mechanism is replaced with throwing
// std::logic_error, since the Domain layer must not depend on Drogon's
// logging macros (design.md §4.1 rule 1).
#include <authforge/common/error/ErrorCatalog.h>

#include <array>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace authforge::common::error
{

namespace
{

// --- HTTP status generation (category/numeric rules) ------------------------
// The catalog never hard-codes per-entry HTTP statuses; they are generated from
// the category (and numeric code for NETWORK) so the registered value is, by
// construction, consistent.
int httpStatusFor(ErrorCategory category, int numericCode)
{
    switch (category)
    {
        case ErrorCategory::VALIDATION:
            return 400;
        case ErrorCategory::AUTHENTICATION:
            return 401;
        case ErrorCategory::AUTHORIZATION:
            return 403;
        case ErrorCategory::NETWORK:
            // TIMEOUT (1002) -> 504 Gateway Timeout, other network -> 502 Bad Gateway.
            return (numericCode == 1002) ? 504 : 502;
        case ErrorCategory::DATABASE:
        case ErrorCategory::INTERNAL:
        case ErrorCategory::UNKNOWN:
        default:
            return 500;
    }
}

// Raw (pre-HTTP-status) definition of an Application catalog entry. The httpStatus
// is filled in by httpStatusFor() when the immutable table is materialized,
// UNLESS httpStatusOverride is non-zero, in which case that explicit value wins.
//
// The explicit override exists to preserve pre-migration HTTP statuses for
// errors whose semantics differ from their category default: e.g. a
// VALIDATION-class "resource not found" must keep returning 404 (and
// "resource conflict" 409) even though the VALIDATION default is 400.
struct RawEntry
{
    std::string_view code;
    int numericCode;
    ErrorCategory category;
    std::string_view defaultMessage;
    std::string_view description;
    int httpStatusOverride = 0;  ///< 0 -> derive from httpStatusFor(); >0 -> use as-is.
};

// Verbatim copy of oauth2::error::ErrorCatalog's 25-entry table (numeric
// codes/messages preserved unchanged).
const std::array<RawEntry, 25> &rawEntries()
{
    static const std::array<RawEntry, 25> kEntries = {{
      // NETWORK (1000-1099)
      {"NET_CONNECTION_FAILED",
       1001,
       ErrorCategory::NETWORK,
       "上游连接失败",
       "上游服务连接失败（NETWORK 类）"},
      {"NET_TIMEOUT", 1002, ErrorCategory::NETWORK, "请求超时", "上游服务请求超时（NETWORK 类）"},

      // DATABASE (2000-2099)
      {"DB_CONNECTION_ERROR",
       2001,
       ErrorCategory::DATABASE,
       "服务暂时不可用",
       "数据库连接失败（DATABASE 类）"},
      {"DB_QUERY_ERROR",
       2002,
       ErrorCategory::DATABASE,
       "服务暂时不可用",
       "数据库查询执行失败（DATABASE 类）"},
      {"DB_CONSTRAINT_VIOLATION",
       2003,
       ErrorCategory::DATABASE,
       "数据冲突",
       "数据库约束冲突（DATABASE 类）"},

      // VALIDATION (3000-3099)
      {"VALIDATION_INVALID_INPUT",
       3001,
       ErrorCategory::VALIDATION,
       "输入参数有误",
       "输入参数校验失败（VALIDATION 类）"},
      {"VALIDATION_MISSING_REQUIRED_FIELD",
       3002,
       ErrorCategory::VALIDATION,
       "缺少必填字段",
       "缺少必填字段（VALIDATION 类）"},
      {"VALIDATION_FORMAT_ERROR",
       3003,
       ErrorCategory::VALIDATION,
       "格式不正确",
       "字段格式不正确（VALIDATION 类）"},
      {"VALIDATION_RESOURCE_NOT_FOUND",
       3004,
       ErrorCategory::VALIDATION,
       "资源不存在",
       "请求的资源不存在（VALIDATION 类，HTTP 404）",
       404},
      {"VALIDATION_RESOURCE_CONFLICT",
       3005,
       ErrorCategory::VALIDATION,
       "资源已存在或冲突",
       "资源已存在或与现有资源冲突（VALIDATION 类，HTTP 409）",
       409},

      // AUTHENTICATION (4000-4099)
      {"AUTH_INVALID_CREDENTIALS",
       4001,
       ErrorCategory::AUTHENTICATION,
       "用户名或密码错误",
       "凭据无效（AUTHENTICATION 类）"},
      {"AUTH_TOKEN_EXPIRED",
       4002,
       ErrorCategory::AUTHENTICATION,
       "登录已过期",
       "令牌已过期（AUTHENTICATION 类）"},
      {"AUTH_TOKEN_INVALID",
       4003,
       ErrorCategory::AUTHENTICATION,
       "登录凭证无效",
       "令牌无效（AUTHENTICATION 类）"},

      // AUTHORIZATION (5000-5099)
      {"AUTHZ_ACCESS_DENIED",
       5001,
       ErrorCategory::AUTHORIZATION,
       "没有访问权限",
       "访问被拒绝（AUTHORIZATION 类）"},
      {"AUTHZ_INSUFFICIENT_PERMISSIONS",
       5002,
       ErrorCategory::AUTHORIZATION,
       "权限不足",
       "权限不足（AUTHORIZATION 类）"},

      // INTERNAL (6000-6099)
      {"INTERNAL_ERROR",
       6001,
       ErrorCategory::INTERNAL,
       "服务器内部错误",
       "服务器内部错误（INTERNAL 类）"},

      // VALIDATION (3000-3099) —— 注册/登录/凭据/token/设备码/限流补充缺口。
      {"VALIDATION_USERNAME_TAKEN",
       3006,
       ErrorCategory::VALIDATION,
       "该用户名已被注册",
       "注册时用户名重复（VALIDATION 类，HTTP 409）",
       409},
      {"VALIDATION_EMAIL_TAKEN",
       3007,
       ErrorCategory::VALIDATION,
       "该邮箱已被注册",
       "注册时邮箱重复（VALIDATION 类，HTTP 409）",
       409},
      {"VALIDATION_CREDENTIAL_ALREADY_REGISTERED",
       3008,
       ErrorCategory::VALIDATION,
       "该安全密钥已注册，无需重复添加",
       "WebAuthn 凭据重复注册（VALIDATION 类，HTTP 409）",
       409},
      {"VALIDATION_RESET_TOKEN_INVALID",
       3009,
       ErrorCategory::VALIDATION,
       "重置链接已失效，请重新申请",
       "密码重置 token 无效/过期/已用（VALIDATION 类）"},
      {"VALIDATION_VERIFICATION_TOKEN_INVALID",
       3010,
       ErrorCategory::VALIDATION,
       "验证链接已失效，请重新发送邮件",
       "邮箱验证 token 无效/过期/已用（VALIDATION 类）"},
      {"VALIDATION_DEVICE_CODE_INVALID",
       3011,
       ErrorCategory::VALIDATION,
       "设备码无效、已过期或已被处理",
       "设备授权 user_code 未找到/已处理/已过期（VALIDATION 类）"},
      {"VALIDATION_RATE_LIMITED",
       3012,
       ErrorCategory::VALIDATION,
       "请求过于频繁，请稍后重试",
       "Hodor 限流拒绝（VALIDATION 类，HTTP 429）",
       429},

      // AUTHENTICATION (4000-4099) —— MFA 补充缺口。
      {"AUTH_MFA_CODE_INVALID",
       4004,
       ErrorCategory::AUTHENTICATION,
       "验证码不正确",
       "MFA TOTP 校验失败（AUTHENTICATION 类）"},
      {"AUTH_MFA_NOT_CONFIGURED",
       4005,
       ErrorCategory::AUTHENTICATION,
       "尚未设置双重验证，请先完成设置",
       "用户尚未完成 MFA 设置（AUTHENTICATION 类）"},
    }};
    return kEntries;
}

// OAuth2 protocol error codes (RFC 6749 §5.2 base set + RFC 7009/8628 codes).
const std::array<OAuthCatalogEntry, 13> &rawOAuthEntries()
{
    static const std::array<OAuthCatalogEntry, 13> kEntries = {{
      {"invalid_request", 400, "请求参数缺失或无效", ""},
      {"invalid_client", 401, "客户端认证失败", ""},
      {"invalid_grant", 400, "授权许可无效或已过期", ""},
      {"unauthorized_client", 400, "客户端无权使用该授权类型", ""},
      {"unsupported_grant_type", 400, "不支持的授权类型", ""},
      {"invalid_scope", 400, "请求的 scope 无效", ""},
      {"server_error", 500, "服务器内部错误", ""},
      {"temporarily_unavailable", 503, "服务暂时不可用", ""},
      {"access_denied", 403, "授权请求被拒绝（用户无权或拒绝授权）", ""},
      {"unsupported_token_type", 400, "不支持的令牌类型", ""},
      {"authorization_pending", 400, "授权尚未完成，请稍后重试", ""},
      {"slow_down", 400, "轮询过于频繁，请降低频率", ""},
      {"expired_token", 400, "设备码已过期，请重新发起授权", ""},
    }};
    return kEntries;
}

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

bool isKnownCategory(ErrorCategory category)
{
    switch (category)
    {
        case ErrorCategory::NETWORK:
        case ErrorCategory::DATABASE:
        case ErrorCategory::VALIDATION:
        case ErrorCategory::AUTHENTICATION:
        case ErrorCategory::AUTHORIZATION:
        case ErrorCategory::INTERNAL:
        case ErrorCategory::UNKNOWN:
            return true;
    }
    return false;
}

}  // namespace

NumericSegment ErrorCatalog::segmentFor(ErrorCategory category)
{
    switch (category)
    {
        case ErrorCategory::NETWORK:
            return {1000, 1099};
        case ErrorCategory::DATABASE:
            return {2000, 2099};
        case ErrorCategory::VALIDATION:
            return {3000, 3099};
        case ErrorCategory::AUTHENTICATION:
            return {4000, 4099};
        case ErrorCategory::AUTHORIZATION:
            return {5000, 5099};
        case ErrorCategory::INTERNAL:
            return {6000, 6099};
        case ErrorCategory::UNKNOWN:
        default:
            // UNKNOWN owns no numeric segment; return an empty (min > max) range.
            return {1, 0};
    }
}

const std::vector<CatalogEntry> &ErrorCatalog::allEntries()
{
    static const std::vector<CatalogEntry> kEntries = [] {
        std::vector<CatalogEntry> v;
        const auto &raw = rawEntries();
        v.reserve(raw.size());
        for (const auto &r : raw)
        {
            v.push_back(
              CatalogEntry{
                r.code,
                r.numericCode,
                r.category,
                r.httpStatusOverride > 0 ? r.httpStatusOverride
                                         : httpStatusFor(r.category, r.numericCode),
                r.defaultMessage,
                r.description,
              }
            );
        }
        return v;
    }();
    return kEntries;
}

const std::vector<OAuthCatalogEntry> &ErrorCatalog::allOAuthEntries()
{
    static const std::vector<OAuthCatalogEntry> kEntries = [] {
        const auto &raw = rawOAuthEntries();
        return std::vector<OAuthCatalogEntry>(raw.begin(), raw.end());
    }();
    return kEntries;
}

const CatalogEntry *ErrorCatalog::find(std::string_view code)
{
    for (const auto &e : allEntries())
    {
        if (e.code == code)
        {
            return &e;
        }
    }
    return nullptr;
}

const CatalogEntry *ErrorCatalog::findByNumeric(int numericCode)
{
    for (const auto &e : allEntries())
    {
        if (e.numericCode == numericCode)
        {
            return &e;
        }
    }
    return nullptr;
}

const OAuthCatalogEntry *ErrorCatalog::findOAuth(std::string_view error)
{
    for (const auto &e : allOAuthEntries())
    {
        if (e.error == error)
        {
            return &e;
        }
    }
    return nullptr;
}

const CatalogEntry &ErrorCatalog::internalError()
{
    const CatalogEntry *entry = find("INTERNAL_ERROR");
    if (entry == nullptr)
    {
        // INTERNAL_ERROR is a mandatory catalog entry; its absence is a build
        // defect. Domain layer: throw rather than LOG_FATAL/abort (no Drogon
        // dependency allowed here).
        throw std::logic_error("ErrorCatalog: mandatory entry INTERNAL_ERROR is missing");
    }
    return *entry;
}

void ErrorCatalog::validateInvariants()
{
    std::vector<std::string> violations;

    const auto &entries = allEntries();

    std::unordered_set<std::string_view> seenCodes;
    std::unordered_set<int> seenNumeric;

    for (const auto &e : entries)
    {
        const std::string codeStr(e.code);

        if (e.code.empty())
        {
            violations.push_back("entry has empty code");
        }
        else if (!seenCodes.insert(e.code).second)
        {
            violations.push_back("duplicate code: " + codeStr);
        }

        if (!isKnownCategory(e.category))
        {
            violations.push_back("entry '" + codeStr + "' has unknown category");
        }

        if (!seenNumeric.insert(e.numericCode).second)
        {
            violations.push_back(
              "duplicate numeric_code " + std::to_string(e.numericCode) + " (code " + codeStr + ")"
            );
        }
        const NumericSegment seg = segmentFor(e.category);
        if (e.numericCode < seg.min || e.numericCode > seg.max)
        {
            violations.push_back(
              "numeric_code " + std::to_string(e.numericCode) + " for code '" + codeStr +
              "' out of " + categoryName(e.category) + " segment [" + std::to_string(seg.min) +
              "," + std::to_string(seg.max) + "]"
            );
        }

        if (e.httpStatus < 100 || e.httpStatus > 599)
        {
            violations.push_back(
              "httpStatus " + std::to_string(e.httpStatus) + " for code '" + codeStr +
              "' out of range [100,599]"
            );
        }

        if (e.defaultMessage.empty())
        {
            violations.push_back("empty default message for code '" + codeStr + "'");
        }

        if (e.description.empty() || e.description.size() > 200)
        {
            violations.push_back("description length out of [1,200] for code '" + codeStr + "'");
        }
    }

    const auto &oauthEntries = allOAuthEntries();
    std::unordered_map<std::string_view, int> oauthCounts;
    for (const auto &o : oauthEntries)
    {
        const std::string errStr(o.error);
        if (o.error.empty())
        {
            violations.push_back("oauth entry has empty error code");
        }
        if (o.defaultErrorDesc.empty())
        {
            violations.push_back(
              "oauth entry '" + errStr + "' has empty default error_description"
            );
        }
        if (o.httpStatus < 100 || o.httpStatus > 599)
        {
            violations.push_back(
              "oauth entry '" + errStr + "' httpStatus " + std::to_string(o.httpStatus) +
              " out of range [100,599]"
            );
        }
        ++oauthCounts[o.error];
    }

    static constexpr std::array<std::string_view, 13> kRequiredOAuthCodes = {{
      "invalid_request",
      "invalid_client",
      "invalid_grant",
      "unauthorized_client",
      "unsupported_grant_type",
      "invalid_scope",
      "server_error",
      "temporarily_unavailable",
      "access_denied",
      "unsupported_token_type",
      "authorization_pending",
      "slow_down",
      "expired_token",
    }};
    for (const auto &required : kRequiredOAuthCodes)
    {
        const auto it = oauthCounts.find(required);
        const int count = (it == oauthCounts.end()) ? 0 : it->second;
        if (count != 1)
        {
            violations.push_back(
              "oauth code '" + std::string(required) + "' must appear exactly once, found " +
              std::to_string(count)
            );
        }
    }

    if (!violations.empty())
    {
        std::ostringstream oss;
        oss << "ErrorCatalog::validateInvariants() failed with " << violations.size()
            << " violation(s):";
        for (const auto &v : violations)
        {
            oss << "\n  - " << v;
        }
        // Fail-fast: a defective catalog must not reach production. Domain
        // layer: throw rather than LOG_FATAL/abort (no Drogon dependency).
        throw std::logic_error(oss.str());
    }
}

}  // namespace authforge::common::error
