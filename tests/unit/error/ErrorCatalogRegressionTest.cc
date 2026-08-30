#include <drogon/drogon_test.h>
#include <fulla/common/error/ErrorCatalog.h>

#include <string>
#include <vector>

using namespace fulla::common::error;

// Feature: error-code-message-standardization
// Example/regression test (specific preserved values, not a randomized property
// test) guarding Requirements 3.6 and 11.5: the 14 existing Numeric_Error_Code
// integer values MUST stay unchanged, every Application numeric code MUST fall
// inside its Error_Category segment, and the full OAuth2 protocol error-code set
// MUST remain registered with no omissions.

// --- Requirement 3.6 / 11.5: preserved numeric values for the 14 existing codes.
DROGON_TEST(Unit_P0_ErrorCatalog_Regression_ExistingNumericCodesUnchanged)
{
    struct Expected
    {
        const char *code;
        int numericCode;
    };

    // Exact integer values that pre-date the standardization and must be preserved.
    const std::vector<Expected> kExpected = {
      {"NET_CONNECTION_FAILED", 1001},
      {"NET_TIMEOUT", 1002},
      {"DB_CONNECTION_ERROR", 2001},
      {"DB_QUERY_ERROR", 2002},
      {"DB_CONSTRAINT_VIOLATION", 2003},
      {"VALIDATION_INVALID_INPUT", 3001},
      {"VALIDATION_MISSING_REQUIRED_FIELD", 3002},
      {"VALIDATION_FORMAT_ERROR", 3003},
      {"AUTH_INVALID_CREDENTIALS", 4001},
      {"AUTH_TOKEN_EXPIRED", 4002},
      {"AUTH_TOKEN_INVALID", 4003},
      {"AUTHZ_ACCESS_DENIED", 5001},
      {"AUTHZ_INSUFFICIENT_PERMISSIONS", 5002},
      {"INTERNAL_ERROR", 6001},
    };

    for (const auto &exp : kExpected)
    {
        const CatalogEntry *entry = ErrorCatalog::find(exp.code);
        REQUIRE(entry != nullptr);
        // Numeric_Error_Code must equal the preserved baseline value.
        CHECK(entry->numericCode == exp.numericCode);
        // Reverse lookup by numeric code must resolve back to the same entry.
        const CatalogEntry *byNumeric = ErrorCatalog::findByNumeric(exp.numericCode);
        REQUIRE(byNumeric != nullptr);
        CHECK(std::string(byNumeric->code) == std::string(exp.code));
    }

    // The 14 pre-existing codes above keep their integer values (verified in the
    // loop). 方案 A adds exactly 2 resource-oriented VALIDATION codes
    // (VALIDATION_RESOURCE_NOT_FOUND/CONFLICT, Requirement 11.4) to preserve the
    // pre-migration 404/409 statuses. The auth-flow-error-code-gaps feature adds
    // a further 9 codes (G1-G6). #78 adds AUTH_INVALID_ID_TOKEN_HINT (end_session
    // id_token_hint verification failure, HTTP 400). #88 adds
    // VALIDATION_REDIRECT_URI_NOT_REGISTERED (end_session unregistered
    // post_logout_redirect_uri, HTTP 400), for a total of 27 registered
    // Application codes; no others may be introduced silently.
    CHECK(ErrorCatalog::allEntries().size() == kExpected.size() + 2 + 9 + 1 + 1 + 2);
    // #78: the new entry is registered exactly once, in the AUTHENTICATION
    // segment, with the explicit 400 status override.
    const CatalogEntry *hintEntry = ErrorCatalog::find("AUTH_INVALID_ID_TOKEN_HINT");
    REQUIRE(hintEntry != nullptr);
    CHECK(hintEntry->numericCode == 4006);
    CHECK(hintEntry->category == ErrorCategory::AUTHENTICATION);
    CHECK(hintEntry->httpStatus == 400);
    // #88: VALIDATION segment, next free numeric 3013, explicit 400.
    const CatalogEntry *redirectEntry = ErrorCatalog::find("VALIDATION_REDIRECT_URI_NOT_REGISTERED");
    REQUIRE(redirectEntry != nullptr);
    CHECK(redirectEntry->numericCode == 3013);
    CHECK(redirectEntry->category == ErrorCategory::VALIDATION);
    CHECK(redirectEntry->httpStatus == 400);
}

// --- Requirement 3.6: every Application numeric code falls inside its segment.
DROGON_TEST(Unit_P0_ErrorCatalog_Regression_NumericCodesWithinCategorySegment)
{
    for (const auto &entry : ErrorCatalog::allEntries())
    {
        const NumericSegment seg = ErrorCatalog::segmentFor(entry.category);
        // The numeric code must lie within [seg.min, seg.max] for its category.
        CHECK(entry.numericCode >= seg.min);
        CHECK(entry.numericCode <= seg.max);
    }
}

// --- Requirement 11.5 / 3.6: OAuth2 protocol code set present with no omissions.
DROGON_TEST(Unit_P0_ErrorCatalog_Regression_OAuthProtocolCodeSetComplete)
{
    // RFC 6749 §5.2 base set plus RFC 7009/8628 codes that must all be registered.
    const std::vector<std::string> kRequiredOAuthCodes = {
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
    };

    for (const auto &code : kRequiredOAuthCodes)
    {
        const OAuthCatalogEntry *entry = ErrorCatalog::findOAuth(code);
        // No omissions: each required protocol code resolves to a catalog entry.
        REQUIRE(entry != nullptr);
        CHECK(std::string(entry->error) == code);
    }

    // The catalog registers exactly the required protocol codes (no omissions).
    CHECK(ErrorCatalog::allOAuthEntries().size() == kRequiredOAuthCodes.size());
}
