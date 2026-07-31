#pragma once

#include <drogon/HttpResponse.h>
#include <string>
#include <functional>

namespace authforge::common::error
{

/**
 * @brief Handles OAuth2.0 standardized error responses (RFC 6749 Section 5.2)
 */
class OAuth2ErrorHandler
{
  public:
    /**
     * @brief Send an RFC 6749 §5.2 compliant error response.
     *
     * The JSON body keeps the exact shape { "error", optional "error_description",
     * optional "error_uri" }. The HTTP status code and the default
     * error_description are driven by the single-source-of-truth ErrorCatalog:
     * when @p description is empty, the catalog's registered default
     * error_description for @p errorCode is used as a fallback.
     *
     * Always sets Cache-Control: no-store, Pragma: no-cache and
     * Content-Type: application/json.
     *
     * @param callback     Drogon response callback.
     * @param errorCode    Protocol error code (e.g. invalid_request, invalid_client).
     * @param description  Optional error_description; when empty, falls back to the
     *                     catalog default for @p errorCode.
     * @param errorUri     Optional error_uri.
     * @param authScheme   Optional authentication scheme name used by the client via
     *                     the Authorization request header (e.g. "Basic"). When
     *                     @p errorCode is "invalid_client" and this is non-empty, a
     *                     matching WWW-Authenticate challenge header is set
     *                     (RFC 6749 §5.2 / Requirement 2.9). HTTP 401 is preserved.
     */
    static void sendErrorResponse(
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &errorCode,
      const std::string &description = "",
      const std::string &errorUri = "",
      const std::string &authScheme = ""
    );

    static ::drogon::HttpStatusCode getHttpStatusCode(const std::string &errorCode);

    // Standard OAuth2 Error Codes
    static constexpr const char *INVALID_REQUEST = "invalid_request";
    static constexpr const char *INVALID_CLIENT = "invalid_client";
    static constexpr const char *INVALID_GRANT = "invalid_grant";
    static constexpr const char *UNAUTHORIZED_CLIENT = "unauthorized_client";
    static constexpr const char *UNSUPPORTED_GRANT_TYPE = "unsupported_grant_type";
    static constexpr const char *INVALID_SCOPE = "invalid_scope";
    static constexpr const char *SERVER_ERROR = "server_error";
    static constexpr const char *TEMPORARILY_UNAVAILABLE = "temporarily_unavailable";
};

}  // namespace authforge::common::error
