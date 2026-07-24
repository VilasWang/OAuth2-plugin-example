#include <oauth2/error/OAuth2ErrorHandler.h>
#include <oauth2/error/ErrorCatalog.h>
#include <json/json.h>

namespace authforge::common::error
{

namespace
{

// Map a numeric HTTP status (as registered in the ErrorCatalog) to the
// ::drogon::HttpStatusCode enum. Only the status codes that the OAuth2 protocol
// catalog can register are listed; anything else falls back to 400 Bad Request,
// which preserves the historical default for codes outside the catalog.
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
        case 500:
            return ::drogon::k500InternalServerError;
        case 503:
            return ::drogon::k503ServiceUnavailable;
        default:
            return ::drogon::k400BadRequest;
    }
}

}  // namespace

void OAuth2ErrorHandler::sendErrorResponse(
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &errorCode,
  const std::string &description,
  const std::string &errorUri,
  const std::string &authScheme
)
{
    // Look up the protocol code in the single source of truth (ErrorCatalog).
    const OAuthCatalogEntry *entry = ErrorCatalog::findOAuth(errorCode);

    Json::Value error;
    error["error"] = errorCode;

    // error_description: use the caller-provided value, otherwise fall back to the
    // catalog's registered default (Client_Safe_Message) for this protocol code.
    if (!description.empty())
    {
        error["error_description"] = description;
    }
    else if (entry != nullptr && !entry->defaultErrorDesc.empty())
    {
        error["error_description"] = std::string(entry->defaultErrorDesc);
    }

    // error_uri: prefer the caller-provided value, otherwise the catalog default.
    if (!errorUri.empty())
    {
        error["error_uri"] = errorUri;
    }
    else if (entry != nullptr && !entry->errorUri.empty())
    {
        error["error_uri"] = std::string(entry->errorUri);
    }

    auto resp = ::drogon::HttpResponse::newHttpJsonResponse(error);

    // RFC 6749 §5.2 caching behavior + explicit JSON content type.
    resp->addHeader("Cache-Control", "no-store");
    resp->addHeader("Pragma", "no-cache");
    resp->setContentTypeCode(::drogon::CT_APPLICATION_JSON);

    // Determine HTTP status code from the catalog (Requirement 2.7).
    resp->setStatusCode(getHttpStatusCode(errorCode));

    // RFC 6749 §5.2 / Requirement 2.9: when the client attempted to authenticate
    // via the Authorization request header and authentication failed
    // (invalid_client), respond with a WWW-Authenticate challenge matching the
    // scheme the client used. HTTP 401 is preserved by getHttpStatusCode.
    if (errorCode == INVALID_CLIENT && !authScheme.empty())
    {
        resp->addHeader("WWW-Authenticate", authScheme + " realm=\"OAuth2 Client Authentication\"");
    }

    callback(resp);
}

::drogon::HttpStatusCode OAuth2ErrorHandler::getHttpStatusCode(const std::string &errorCode)
{
    // The ErrorCatalog is the single source of truth for protocol error HTTP
    // statuses (Requirement 2.7): invalid_client -> 401, server_error -> 500,
    // temporarily_unavailable -> 503, the remaining RFC 6749 token endpoint codes
    // -> 400, etc.
    if (const OAuthCatalogEntry *entry = ErrorCatalog::findOAuth(errorCode))
    {
        return toDrogonStatus(entry->httpStatus);
    }

    // Safe fallback for any code not registered in the catalog: HTTP 400 Bad
    // Request, preserving the historical default behavior.
    return ::drogon::k400BadRequest;
}

}  // namespace authforge::common::error
