#pragma once

#include <exception>
#include <string>
#include <json/json.h>

namespace authforge::common::error
{

// Error_Category enum. The enum NAMES are stable and must not change
// (Requirement 11.5): NETWORK / DATABASE / VALIDATION / AUTHENTICATION /
// AUTHORIZATION / INTERNAL / UNKNOWN.
enum class ErrorCategory
{
    NETWORK,         // Network-related errors
    DATABASE,        // Database errors
    VALIDATION,      // Input validation errors
    AUTHENTICATION,  // Authentication errors
    AUTHORIZATION,   // Authorization errors
    INTERNAL,        // Internal system errors
    UNKNOWN          // Unknown errors
};

// Returns the canonical string name of an Error_Category (matches the
// Error_Category enum set used in the Error Envelope `category` field).
const char *toString(ErrorCategory category);

/**
 * @brief A backend Application error, rendered as an Error Envelope.
 *
 * The `code` field is now the stable STRING Error_Code (e.g.
 * "AUTH_INVALID_CREDENTIALS"). The integer Numeric_Error_Code has been moved
 * out of this struct and is looked up on demand from the ErrorCatalog
 * (single source of truth). See design AD-2.
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
    /// fall back to the internal-error entry (Requirement 5.5).
    static Error fromException(
      const std::exception &e,
      ErrorCategory category,
      std::string requestId
    );
};

}  // namespace authforge::common::error
