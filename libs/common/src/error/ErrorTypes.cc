// Task 13 (fulla-sdk-refactor, design.md §4.1/§5.1/§6): framework-agnostic
// port of the Error struct methods currently implemented in
// OAuth2Plugin/src/error/ErrorHandler.cc (toString/toHttpStatusCode/
// hasNumericCode/numericCode/toJson/fromCode/fromException). The
// ErrorHandler class itself (logError/handleDbException/etc, which depend on
// Drogon's DrogonDbException/HttpRequestPtr/LOG_* macros) is NOT ported here
// -- it stays Adapter-side. This file only carries the pure Error value-type
// logic that has no Drogon dependency.
#include <fulla/common/error/ErrorTypes.h>
#include <fulla/common/error/ErrorCatalog.h>

namespace fulla::common::error
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
// fails to resolve falls back to the internal-error entry.
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
    // The ErrorCatalog is the runtime authority for the HTTP status code.
    // Codes not registered in the catalog fall back to the category mapping
    // so the function is always total.
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
    // Error Envelope: a single top-level `error` object.
    Json::Value errorObj;
    errorObj["code"] = code;
    errorObj["category"] = toString(category);
    errorObj["message"] = message;
    errorObj["request_id"] = requestId;

    // `numeric_code` is present iff the code is registered in the catalog;
    // otherwise the field is fully omitted.
    const CatalogEntry *entry = ErrorCatalog::find(code);
    if (entry != nullptr)
    {
        errorObj["numeric_code"] = entry->numericCode;
    }

    // `details` is only emitted when explicitly requested (non-production);
    // in production mode the key is fully omitted.
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
        // Unmapped exception -> internal-error fallback.
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

}  // namespace fulla::common::error
