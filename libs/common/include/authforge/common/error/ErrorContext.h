#pragma once

#include <optional>

namespace authforge::common::error
{

// ErrorContext centralizes the Production_Mode decision shared by every error
// handling entry point (Application Error Envelope, OAuth2 protocol responses
// and validation responses). It determines whether an error response may carry
// additional diagnostic details (`details`) or must be limited to the
// Client_Safe_Message only.
//
// Default behavior (lifted from validation/HttpResponder):
//   * In a DEBUG build (`#ifdef DEBUG`) detailed errors are always allowed.
//   * Otherwise detailed errors are allowed only when the
//     `DETAILED_VALIDATION_ERRORS` environment variable is set to "1"/"true".
//
// A test-injectable override lets unit/property tests switch Production_Mode on
// and off deterministically without relying on build flags or environment
// variables (see design Property 6 / Property 7).
class ErrorContext
{
  public:
    // Returns true when the backend is NOT running in Production_Mode (i.e. it
    // is allowed to include diagnostic `details` in error responses).
    // Conversely, false means Production_Mode is in effect.
    static bool detailedErrorsAllowed();

    // Test hook: force detailedErrorsAllowed() to return `allowed`, overriding
    // the build flag / environment variable detection.
    //   * setDetailedErrorsOverride(false) -> simulate Production_Mode.
    //   * setDetailedErrorsOverride(true)  -> simulate non-Production_Mode.
    static void setDetailedErrorsOverride(bool allowed);

    // Test hook: remove a previously installed override and fall back to the
    // default build flag / environment variable detection.
    static void clearDetailedErrorsOverride();

  private:
    // Computes the default decision from the build flag / environment variable.
    static bool defaultDetailedErrorsAllowed();
};

}  // namespace authforge::common::error
