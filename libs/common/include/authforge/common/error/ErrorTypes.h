#pragma once

// Task 13 (authforge-sdk-refactor, design.md §4.1/§5.1/§6): libs/common
// Domain kernel. This is the FRAMEWORK-AGNOSTIC counterpart of
// OAuth2Plugin/include/oauth2/error/ErrorTypes.h -- ported here with the
// Drogon dependency (Json::Value in toJson()/details rendering does NOT
// require Drogon, only jsoncpp, which the Domain layer is explicitly
// allowed to depend on per design.md §4.1 rule 1) removed and the
// request-id / HTTP-status-code lookup kept intact.
//
// Placement note: the pre-existing OAuth2Plugin/include/oauth2/error/*
// headers are NOT touched by this task -- Task 13 is additive (per the M2a
// wave description: "抽 common + 端口 + 去 drogon::utils", carried out via
// small, reviewable steps rather than a single big-bang move). The
// migration of production call sites from oauth2::error to
// authforge::common::error happens in a later M2a task (Task 14/16) once
// libs/common exists and can be linked. This header intentionally
// duplicates (rather than `#include`s) the original so libs/common has no
// dependency edge back into OAuth2Plugin (which does depend on Drogon).
//
// namespace: authforge::common::error (design.md §6 "命名空间同步：
// authforge::common::error/authforge::common::config -> authforge::common::").

#include <exception>
#include <string>
#include <json/json.h>

namespace authforge::common::error
{

/**
 * @brief Stable error category taxonomy. Names MUST NOT change (mirrors
 * the pre-existing authforge::common::error::ErrorCategory contract).
 */
enum class ErrorCategory
{
    NETWORK,         ///< Network-related errors
    DATABASE,        ///< Database errors
    VALIDATION,      ///< Input validation errors
    AUTHENTICATION,  ///< Authentication errors
    AUTHORIZATION,   ///< Authorization errors
    INTERNAL,        ///< Internal system errors
    UNKNOWN          ///< Unknown errors
};

/// Returns the canonical string name of an ErrorCategory (matches the
/// Error_Category enum set used in the Error Envelope `category` field).
const char *toString(ErrorCategory category);

/**
 * @brief A Domain-level error, rendered as an Error Envelope.
 *
 * `code` is the stable STRING Error_Code (e.g. "AUTH_INVALID_CREDENTIALS").
 * The integer Numeric_Error_Code is looked up on demand from ErrorCatalog
 * (single source of truth), not stored here.
 */
struct Error
{
    std::string code;        ///< Stable string Error_Code (looked up in ErrorCatalog).
    ErrorCategory category;  ///< Error classification.
    std::string message;     ///< Client_Safe_Message (in production = Catalog default).
    std::string details;     ///< Internal_Detail; only emitted when includeDetails is set.
    std::string requestId;   ///< Request_ID correlating the response with logs.

    /// HTTP status code for this error. Looks up the ErrorCatalog; falls back to
    /// the category mapping for codes not registered in the catalog.
    int toHttpStatusCode() const;

    /// True iff `code` is registered in the ErrorCatalog (and therefore has a
    /// Numeric_Error_Code).
    bool hasNumericCode() const;

    /// The Numeric_Error_Code registered for `code` in the ErrorCatalog. Returns
    /// 0 when `code` is not registered; callers should guard with hasNumericCode().
    int numericCode() const;

    /// Render this error as an Error Envelope (top-level single `error` object).
    /// When includeDetails is false (Production_Mode) the `details` key is fully
    /// omitted. `numeric_code` is only present when the code is registered.
    Json::Value toJson(bool includeDetails) const;

    /// Build an Error from a string Error_Code, populating category and the
    /// default Client_Safe_Message from the ErrorCatalog. Unregistered codes
    /// fall back to the internal-error entry (INTERNAL_ERROR / numeric 6001).
    static Error fromCode(std::string code, std::string requestId);

    /// Build an Error from an exception and a category hint. The exception text
    /// is captured into `details` (Internal_Detail). Codes that cannot be mapped
    /// fall back to the internal-error entry.
    static Error fromException(
      const std::exception &e,
      ErrorCategory category,
      std::string requestId
    );
};

}  // namespace authforge::common::error
