#pragma once

#include <fulla/common/error/ErrorTypes.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <functional>
#include <string>
#include <vector>

namespace fulla::common::error
{

/**
 * @brief A single field-level validation failure.
 *
 * No pre-existing FieldError type exists in the codebase (validation code uses
 * fulla::drogon::validation::Result, a different namespace concerned with rule
 * evaluation), so ErrorResponder defines this minimal structure as the input to
 * respondValidation(). Task 6.1 (HttpResponder) is expected to construct these
 * from its validation results when delegating to ErrorResponder.
 */
struct FieldError
{
    std::string field;   ///< Name of the field that failed validation.
    std::string reason;  ///< Human-readable failure reason (Client_Safe_Message safe).
};

/**
 * @brief Unified entry point that produces Error Envelope responses for all
 *        Application_Endpoint errors (design AD-1 / Requirement 7.1).
 *
 * Responsibilities (design §3, ErrorResponder):
 *   1. Look up the Error_Code in the single-source-of-truth ErrorCatalog.
 *   2. In Production_Mode force the catalog default Client_Safe_Message and omit
 *      `details` (Requirement 5.1 / 5.6).
 *   3. Inject the Request_ID via RequestId::resolve (Requirement 6.1).
 *   4. Decide whether to include `details` from ErrorContext (Requirement 5.4).
 *   5. Set the catalog-registered HTTP status code and Content-Type:
 *      application/json (Requirement 1.4 / 4.7).
 *   6. Log the error with the Internal_Detail and Request_ID (Requirement 5.2 / 6.2).
 *
 * An unregistered code is never thrown and never leaked: it is logged with
 * LOG_ERROR (server side) and the response falls back to INTERNAL_ERROR.
 */
class ErrorResponder
{
  public:
    using Callback = std::function<void(const ::drogon::HttpResponsePtr &)>;

    /**
     * @brief Main entry point: build and send an Error Envelope for @p code.
     *
     * @param req           Inbound request (used to resolve the Request_ID).
     * @param cb            Drogon response callback.
     * @param code          Stable string Error_Code; unregistered codes fall back
     *                      to INTERNAL_ERROR (never thrown, never leaked).
     * @param detailForLog  Internal_Detail recorded to the server log only.
     * @param clientDetails Diagnostic detail surfaced in the Envelope `details`
     *                      ONLY when detailed errors are allowed (non-Production_Mode).
     */
    static void respond(
      const ::drogon::HttpRequestPtr &req,
      Callback &&cb,
      std::string code,
      std::string detailForLog = "",
      std::string clientDetails = ""
    );

    /**
     * @brief Convenience entry point for VALIDATION-class errors.
     *
     * Produces a VALIDATION Error Envelope (code VALIDATION_INVALID_INPUT,
     * category VALIDATION, HTTP 400). When detailed errors are allowed the
     * failing field names and reasons are included in `details`
     * (Requirement 7.4 / 7.6).
     */
    static void respondValidation(
      const ::drogon::HttpRequestPtr &req,
      Callback &&cb,
      const std::vector<FieldError> &fieldErrors
    );

    /**
     * @brief Convenience entry point for caught exceptions.
     *
     * Maps the exception via Error::fromException using @p category as a hint;
     * unmapped exceptions fall back to INTERNAL_ERROR (Requirement 5.5). The
     * exception text is recorded as Internal_Detail in the log.
     */
    static void respondException(
      const ::drogon::HttpRequestPtr &req,
      Callback &&cb,
      const std::exception &e,
      ErrorCategory category
    );

    /**
     * @brief Build the HttpResponsePtr for an already-constructed Error.
     *
     * Reused by the global exception handler (task 7.1). Sets the catalog HTTP
     * status code and Content-Type: application/json, and decides whether to
     * include `details` from ErrorContext. If @p error has no Request_ID it is
     * resolved from @p req so the response always carries one.
     */
    static ::drogon::HttpResponsePtr buildResponse(
      const ::drogon::HttpRequestPtr &req,
      const Error &error
    );
};

}  // namespace fulla::common::error
