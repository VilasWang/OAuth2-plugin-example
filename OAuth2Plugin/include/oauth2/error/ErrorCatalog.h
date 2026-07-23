#pragma once

#include <oauth2/error/ErrorTypes.h>
#include <string_view>
#include <vector>

namespace authforge::common::error
{

/**
 * @brief A single Application error catalog entry (single source of truth).
 *
 * Each entry maps a stable string Error_Code to its numeric code, category,
 * HTTP status code, default Client_Safe_Message and a short description.
 * The numeric code MUST fall inside the segment range of its category
 * (see ErrorCatalog::segmentFor) and the HTTP status code is generated from
 * the Requirement 4 category/numeric mapping rules.
 */
struct CatalogEntry
{
    std::string_view code;   ///< Stable string Error_Code, unique in catalog.
    int numericCode;         ///< Numeric_Error_Code, unique, inside category segment.
    ErrorCategory category;  ///< Error classification.
    int httpStatus;          ///< 100..599, generated per Requirement 4 rules.
    std::string_view
      defaultMessage;              ///< Default Client_Safe_Message (non-empty, no Internal_Detail).
    std::string_view description;  ///< Short description, length 1..200.
};

/**
 * @brief A single OAuth2 protocol error catalog entry (RFC 6749 §5.2 family).
 */
struct OAuthCatalogEntry
{
    std::string_view error;             ///< Protocol string error code (e.g. invalid_request).
    int httpStatus;                     ///< Registered HTTP status code.
    std::string_view defaultErrorDesc;  ///< Default error_description (Client_Safe_Message).
    std::string_view errorUri;          ///< Optional error_uri; empty means none.
};

/**
 * @brief Inclusive numeric segment range for a category.
 */
struct NumericSegment
{
    int min;
    int max;
};

/**
 * @brief Single authoritative source for every Error_Code and every OAuth2
 *        protocol error code used by the backend.
 *
 * The catalog is defined at compile time as static tables. All runtime entry
 * points, documentation generation and tests read from these tables so there
 * are no scattered hard-coded mappings.
 */
class ErrorCatalog
{
  public:
    /// Look up an Application Error_Code. Returns nullptr if not registered.
    static const CatalogEntry *find(std::string_view code);

    /// Look up an Application entry by its Numeric_Error_Code. Returns nullptr if not registered.
    static const CatalogEntry *findByNumeric(int numericCode);

    /// Look up an OAuth2 protocol error code. Returns nullptr if not registered.
    static const OAuthCatalogEntry *findOAuth(std::string_view error);

    /// Full enumeration of Application entries (for documentation/test enumeration).
    static const std::vector<CatalogEntry> &allEntries();

    /// Full enumeration of OAuth2 protocol entries.
    static const std::vector<OAuthCatalogEntry> &allOAuthEntries();

    /// Internal-error fallback entry (Numeric_Error_Code 6001) for unmapped exceptions.
    static const CatalogEntry &internalError();

    /// Inclusive numeric segment for a category (UNKNOWN has no segment -> {1, 0}).
    static NumericSegment segmentFor(ErrorCategory category);

    /**
     * @brief Startup/self-check asserting the catalog invariants (Property 5).
     *
     * Verifies: code uniqueness, numeric_code uniqueness and segment membership,
     * httpStatus in [100, 599], non-empty default Client_Safe_Message, description
     * length 1..200, and that every required OAuth2 protocol code is covered by
     * exactly one entry. On any violation it fails fast (fatal exit) to prevent a
     * defective build from being released.
     */
    static void validateInvariants();
};

}  // namespace authforge::common::error
