#include <fulla/drogon/error/ErrorResponder.h>
#include <fulla/common/error/ErrorCatalog.h>
#include <fulla/common/error/ErrorContext.h>
#include <fulla/drogon/error/ErrorHandler.h>
#include <fulla/drogon/error/RequestId.h>

#include <drogon/drogon.h>
#include <json/json.h>

#include <sstream>
#include <utility>

namespace fulla::common::error
{

namespace
{

// Map a numeric HTTP status (as registered in the ErrorCatalog) to the
// ::drogon::HttpStatusCode enum. Covers every status the Application catalog can
// register (400/401/403/404/409/429/500/502/503/504); anything unexpected
// defaults to 500 Internal Server Error, keeping the function total.
::drogon::HttpStatusCode toDrogonStatus(int httpStatus)
{
    switch (httpStatus)
    {
        case 400:
            return ::drogon::k400BadRequest;
        case 401:
            return ::drogon::k401Unauthorized;
        case 403:
            return ::drogon::k403Forbidden;
        case 404:
            return ::drogon::k404NotFound;
        case 409:
            return ::drogon::k409Conflict;
        case 429:
            return ::drogon::k429TooManyRequests;
        case 502:
            return ::drogon::k502BadGateway;
        case 503:
            return ::drogon::k503ServiceUnavailable;
        case 504:
            return ::drogon::k504GatewayTimeout;
        case 500:
        default:
            return ::drogon::k500InternalServerError;
    }
}

}  // namespace

void ErrorResponder::respond(
  const ::drogon::HttpRequestPtr &req,
  Callback &&cb,
  std::string code,
  std::string detailForLog,
  std::string clientDetails
)
{
    // Resolve the Request_ID first so it is available even on the
    // unregistered-code error path (Requirement 6.1).
    std::string requestId = RequestId::resolve(req);

    // Look up the code in the single source of truth. An unregistered code is a
    // programming error: log it (with the offending code + request_id) and fall
    // back to INTERNAL_ERROR. The original unknown code is NEVER thrown and
    // NEVER leaked to the client (design Error Handling / Requirement 5.5).
    if (ErrorCatalog::find(code) == nullptr)
    {
        LOG_ERROR << "[" << requestId << "] ErrorResponder: unregistered Error_Code '" << code
                  << "', falling back to INTERNAL_ERROR";
        code = std::string(ErrorCatalog::internalError().code);
    }

    // fromCode populates category and the catalog default Client_Safe_Message.
    // In Production_Mode this default message is what the client sees and the
    // `details` key is omitted by buildResponse (Requirement 5.1 / 5.6).
    Error error = Error::fromCode(std::move(code), requestId);

    // Client-facing diagnostic, surfaced in the Envelope `details` only when
    // detailed errors are allowed (non-Production_Mode).
    error.details = std::move(clientDetails);

    // Log the error with the Internal_Detail and request_id (Requirement 5.2 /
    // 6.2). The server log gets the (potentially sensitive) detailForLog rather
    // than the client-facing details.
    Error logEntry = error;
    if (!detailForLog.empty())
    {
        logEntry.details = std::move(detailForLog);
    }
    ErrorHandler::logError(logEntry, "ErrorResponder");

    cb(buildResponse(req, error));
}

void ErrorResponder::respondValidation(
  const ::drogon::HttpRequestPtr &req,
  Callback &&cb,
  const std::vector<FieldError> &fieldErrors
)
{
    std::string requestId = RequestId::resolve(req);

    // VALIDATION-class Envelope: code VALIDATION_INVALID_INPUT, category
    // VALIDATION, HTTP 400 (Requirement 7.4).
    Error error = Error::fromCode("VALIDATION_INVALID_INPUT", std::move(requestId));

    // Field names + failure reasons are diagnostic detail; they are emitted in
    // `details` only when detailed errors are allowed (Requirement 7.6) and are
    // always recorded in the server log.
    if (!fieldErrors.empty())
    {
        std::ostringstream oss;
        bool first = true;
        for (const auto &fe : fieldErrors)
        {
            if (!first)
            {
                oss << "; ";
            }
            first = false;
            oss << fe.field << ": " << fe.reason;
        }
        error.details = oss.str();
    }

    ErrorHandler::logError(error, "ErrorResponder::respondValidation");

    cb(buildResponse(req, error));
}

void ErrorResponder::respondException(
  const ::drogon::HttpRequestPtr &req,
  Callback &&cb,
  const std::exception &e,
  ErrorCategory category
)
{
    std::string requestId = RequestId::resolve(req);

    // Map the exception via the category hint; unmappable exceptions fall back
    // to INTERNAL_ERROR (Requirement 5.5). fromException captures e.what() as
    // Internal_Detail (details) and sets message to the catalog default.
    Error error = Error::fromException(e, category, std::move(requestId));

    // Log the Internal_Detail (e.what()) + request_id (Requirement 5.2).
    ErrorHandler::logError(error, "ErrorResponder::respondException");

    // buildResponse gates `details` on ErrorContext, so in Production_Mode the
    // exception text never reaches the client (Requirement 5.3).
    cb(buildResponse(req, error));
}

::drogon::HttpResponsePtr ErrorResponder::buildResponse(
  const ::drogon::HttpRequestPtr &req,
  const Error &error
)
{
    Error e = error;

    // Always carry a Request_ID even when the caller did not set one
    // (Requirement 6.1).
    if (e.requestId.empty())
    {
        e.requestId = RequestId::resolve(req);
    }

    // Whether to include `details` is the single Production_Mode decision shared
    // by every entry point (Requirement 5.1 / 5.4).
    const bool includeDetails = ErrorContext::detailedErrorsAllowed();

    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(e.toJson(includeDetails));

    // HTTP status is the catalog-registered value (design AD-3 / Requirement 4.7)
    // and the body is always JSON (Requirement 1.4).
    resp->setStatusCode(toDrogonStatus(e.toHttpStatusCode()));
    resp->setContentTypeCode(::drogon::CT_APPLICATION_JSON);

    return resp;
}

}  // namespace fulla::common::error
