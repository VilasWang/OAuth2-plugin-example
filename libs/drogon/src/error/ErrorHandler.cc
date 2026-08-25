// ErrorHandler class (Adapter-side). Only the drogon-dependent ErrorHandler
// methods live here; the pure Error value-type methods (toString/toHttpStatusCode
// /hasNumericCode/numericCode/toJson/fromCode/fromException) and
// representativeCodeFor() now live exclusively in
// libs/common/src/error/ErrorTypes.cc (Task 13 port, framework-agnostic).
// M8 Phase 3: relocated from OAuth2Plugin/src/error/ErrorHandler.cc.
#include <fulla/drogon/error/ErrorHandler.h>
#include <fulla/common/error/ErrorCatalog.h>
#include <fulla/drogon/error/RequestId.h>
#include <drogon/utils/Utilities.h>
#include <sstream>

namespace fulla::common::error
{

void ErrorHandler::logError(const Error &error, const std::string &context)
{
    std::stringstream ss;
    ss << "[" << error.requestId << "] ";
    if (!context.empty())
    {
        ss << context << " - ";
    }
    ss << "[" << error.code << "] " << error.message;
    if (!error.details.empty())
    {
        ss << " | " << error.details;
    }

    // Use appropriate log level based on category
    switch (error.category)
    {
        case ErrorCategory::VALIDATION:
            LOG_WARN << ss.str();
            break;
        case ErrorCategory::AUTHENTICATION:
        case ErrorCategory::AUTHORIZATION:
            LOG_ERROR << ss.str();
            break;
        default:
            LOG_ERROR << ss.str();
    }
}

std::string ErrorHandler::generateRequestId()
{
    // Delegate to RequestId::generate() for consistent UUID format.
    // This function is deprecated; new code should call RequestId::generate() directly.
    return RequestId::generate();
}

Error ErrorHandler::handleDbException(
  const ::drogon::orm::DrogonDbException &e,
  const ::drogon::HttpRequestPtr &req
)
{
    const std::string errStr = e.base().what();

    std::string code;
    if (errStr.find("connection") != std::string::npos)
    {
        code = "DB_CONNECTION_ERROR";
    }
    else if (errStr.find("constraint") != std::string::npos)
    {
        code = "DB_CONSTRAINT_VIOLATION";
    }
    else
    {
        code = "DB_QUERY_ERROR";
    }

    // Use RequestId::resolve(req) to reuse inbound X-Request-ID if present (Req 6.3).
    std::string requestId = req ? RequestId::resolve(req) : RequestId::generate();

    // fromCode sets category and the default Client_Safe_Message from the
    // catalog; the raw driver text is kept only as Internal_Detail (details).
    Error error = Error::fromCode(code, std::move(requestId));
    error.details = errStr;
    return error;
}

Error ErrorHandler::handleValidationError(
  const std::string &field,
  const std::string &reason,
  const ::drogon::HttpRequestPtr &req
)
{
    // Use RequestId::resolve(req) to reuse inbound X-Request-ID if present (Req 6.3).
    std::string requestId = req ? RequestId::resolve(req) : RequestId::generate();

    Error error = Error::fromCode("VALIDATION_INVALID_INPUT", std::move(requestId));
    error.message = reason;
    error.details = "field: " + field;
    return error;
}

}  // namespace fulla::common::error
