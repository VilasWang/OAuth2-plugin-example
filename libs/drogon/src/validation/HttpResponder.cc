#include <authforge/drogon/validation/HttpResponder.h>
#include <authforge/common/error/ErrorTypes.h>
#include <authforge/common/error/ErrorContext.h>
#include <authforge/drogon/error/ErrorHandler.h>
#include <authforge/drogon/error/RequestId.h>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <sstream>

namespace authforge::drogon::validation
{

using authforge::common::error::Error;
using authforge::common::error::ErrorContext;
using authforge::common::error::ErrorHandler;
using authforge::common::error::RequestId;

namespace
{

// Join the raw validation error strings into a single Internal/diagnostic
// detail string. Each entry already embeds a field name and failure reason
// (e.g. "username is required", "code: invalid"), so this preserves the field
// names and reasons required in `details` for non-Production_Mode
// (Requirement 7.6).
std::string joinFieldErrors(const std::vector<std::string> &errors)
{
    std::ostringstream oss;
    bool first = true;
    for (const auto &e : errors)
    {
        if (!first)
        {
            oss << "; ";
        }
        first = false;
        oss << e;
    }
    return oss.str();
}

}  // namespace

Json::Value HttpResponder::buildErrorJson(
  const std::vector<std::string> &errors,
  const ::drogon::HttpRequestPtr &req
)
{
    // VALIDATION-class Error Envelope: code VALIDATION_INVALID_INPUT, category
    // VALIDATION, HTTP 400 (Requirement 7.4). The message is the catalog default
    // Client_Safe_Message; the legacy aliases (`VALIDATION_ERROR`, `reason`,
    // `error_description`, `timestamp`) are intentionally gone (Requirement 7.5).

    // Use RequestId::resolve(req) to reuse inbound X-Request-ID if present (Req 6.3).
    std::string requestId = req ? RequestId::resolve(req) : RequestId::generate();
    Error error = Error::fromCode("VALIDATION_INVALID_INPUT", std::move(requestId));

    // Field names + failure reasons are diagnostic detail. They are surfaced in
    // the Envelope `details` only when detailed errors are allowed
    // (non-Production_Mode, Requirement 7.6) and are always recorded in the log.
    error.details = joinFieldErrors(errors);

    // Server-side log carries the Internal_Detail and Request_ID
    // (Requirement 5.2 / 6.2).
    ErrorHandler::logError(error, "validation::HttpResponder");

    // ErrorContext is the single Production_Mode decision: in Production_Mode the
    // `details` key is fully omitted (Requirement 5.1); otherwise it lists the
    // field names and failure reasons (Requirement 7.6).
    const bool includeDetails = ErrorContext::detailedErrorsAllowed();
    return error.toJson(includeDetails);
}

::drogon::HttpResponsePtr HttpResponder::buildErrorResponse(
  const std::vector<std::string> &errors,
  const ::drogon::HttpRequestPtr &req
)
{
    Json::Value root = buildErrorJson(errors, req);

    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(root);
    // HTTP 400 for the VALIDATION category (Requirement 7.4). Content-Type is set
    // to application/json by newHttpJsonResponse; make it explicit for clarity.
    resp->setStatusCode(::drogon::k400BadRequest);
    resp->setContentTypeCode(::drogon::CT_APPLICATION_JSON);
    return resp;
}

void HttpResponder::respondWithError(
  const std::string &field,
  const std::string &reason,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const ::drogon::HttpRequestPtr &req
)
{
    std::vector<std::string> errors;
    errors.push_back(field + ": " + reason);

    auto resp = buildErrorResponse(errors, req);
    callback(resp);
}

void HttpResponder::respondWithErrors(
  const std::vector<std::string> &errors,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const ::drogon::HttpRequestPtr &req
)
{
    auto resp = buildErrorResponse(errors, req);
    callback(resp);
}

bool HttpResponder::respondIfErrors(
  const std::vector<std::string> &errors,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const ::drogon::HttpRequestPtr &req
)
{
    if (!errors.empty())
    {
        respondWithErrors(errors, std::move(callback), req);
        return true;
    }
    return false;
}

}  // namespace authforge::drogon::validation
