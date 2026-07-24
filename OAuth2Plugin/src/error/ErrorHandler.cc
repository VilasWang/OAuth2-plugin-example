#include <oauth2/error/ErrorHandler.h>
#include <oauth2/error/ErrorCatalog.h>
#include <oauth2/error/RequestId.h>
#include <drogon/utils/Utilities.h>
#include <random>
#include <sstream>
#include <iomanip>

using namespace drogon;
using namespace ::drogon::orm;

namespace authforge::common::error
{

const char *toString(ErrorCategory category)
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
        default:
            return "UNKNOWN";
    }
}

namespace
{

// Choose a representative registered Error_Code for a category, refining a few
// categories by the exception text. Used by Error::fromException so a caught
// exception with a category hint maps to a concrete catalog code; anything that
// fails to resolve falls back to the internal-error entry (Requirement 5.5).
std::string representativeCodeFor(ErrorCategory category, const std::string &text)
{
    switch (category)
    {
        case ErrorCategory::NETWORK:
            if (text.find("timeout") != std::string::npos)
            {
                return "NET_TIMEOUT";
            }
            return "NET_CONNECTION_FAILED";
        case ErrorCategory::DATABASE:
            if (text.find("connection") != std::string::npos)
            {
                return "DB_CONNECTION_ERROR";
            }
            if (text.find("constraint") != std::string::npos)
            {
                return "DB_CONSTRAINT_VIOLATION";
            }
            return "DB_QUERY_ERROR";
        case ErrorCategory::VALIDATION:
            return "VALIDATION_INVALID_INPUT";
        case ErrorCategory::AUTHENTICATION:
            return "AUTH_INVALID_CREDENTIALS";
        case ErrorCategory::AUTHORIZATION:
            return "AUTHZ_ACCESS_DENIED";
        case ErrorCategory::INTERNAL:
        case ErrorCategory::UNKNOWN:
        default:
            return "INTERNAL_ERROR";
    }
}

}  // namespace

int Error::toHttpStatusCode() const
{
    // The ErrorCatalog is the runtime authority for the HTTP status code
    // (design AD-3). Codes not registered in the catalog fall back to the
    // category mapping so the function is always total.
    const CatalogEntry *entry = ErrorCatalog::find(code);
    if (entry != nullptr)
    {
        return entry->httpStatus;
    }

    switch (category)
    {
        case ErrorCategory::VALIDATION:
            return 400;
        case ErrorCategory::AUTHENTICATION:
            return 401;
        case ErrorCategory::AUTHORIZATION:
            return 403;
        case ErrorCategory::NETWORK:
            // No numeric information available for an unregistered code; default
            // to Bad Gateway (TIMEOUT->504 only applies to registered NET_TIMEOUT).
            return 502;
        case ErrorCategory::DATABASE:
        case ErrorCategory::INTERNAL:
        case ErrorCategory::UNKNOWN:
        default:
            return 500;
    }
}

bool Error::hasNumericCode() const
{
    return ErrorCatalog::find(code) != nullptr;
}

int Error::numericCode() const
{
    const CatalogEntry *entry = ErrorCatalog::find(code);
    return entry != nullptr ? entry->numericCode : 0;
}

Json::Value Error::toJson(bool includeDetails) const
{
    // Error Envelope: a single top-level `error` object (Requirement 1.1).
    Json::Value errorObj;
    errorObj["code"] = code;
    errorObj["category"] = toString(category);
    errorObj["message"] = message;
    errorObj["request_id"] = requestId;

    // `numeric_code` is present iff the code is registered in the catalog;
    // otherwise the field is fully omitted (Requirement 1.3, 1.7).
    const CatalogEntry *entry = ErrorCatalog::find(code);
    if (entry != nullptr)
    {
        errorObj["numeric_code"] = entry->numericCode;
    }

    // `details` is only emitted when explicitly requested (non-Production_Mode);
    // in Production_Mode the key is fully omitted (Requirement 5.1).
    if (includeDetails && !details.empty())
    {
        errorObj["details"] = details;
    }

    Json::Value root;
    root["error"] = errorObj;
    return root;
}

Error Error::fromCode(std::string code, std::string requestId)
{
    const CatalogEntry *entry = ErrorCatalog::find(code);
    if (entry == nullptr)
    {
        // Unregistered code -> internal-error fallback (INTERNAL_ERROR / 6001).
        const CatalogEntry &fallback = ErrorCatalog::internalError();
        return Error{
          std::string(fallback.code),
          fallback.category,
          std::string(fallback.defaultMessage),
          "",
          std::move(requestId)
        };
    }

    return Error{
      std::move(code), entry->category, std::string(entry->defaultMessage), "", std::move(requestId)
    };
}

Error Error::fromException(const std::exception &e, ErrorCategory category, std::string requestId)
{
    const std::string what = e.what();
    const std::string code = representativeCodeFor(category, what);

    const CatalogEntry *entry = ErrorCatalog::find(code);
    if (entry == nullptr)
    {
        // Unmapped exception -> internal-error fallback (Requirement 5.5).
        entry = &ErrorCatalog::internalError();
    }

    return Error{
      std::string(entry->code),
      entry->category,
      std::string(entry->defaultMessage),
      what,  // Internal_Detail captured for logs / non-production details.
      std::move(requestId)
    };
}

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
  const DrogonDbException &e,
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

}  // namespace authforge::common::error
