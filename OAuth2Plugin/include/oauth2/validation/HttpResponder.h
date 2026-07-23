#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <vector>
#include <string>
#include <functional>
#include <json/json.h>

namespace authforge::drogon::validation
{

// HttpResponder turns validation failures into HTTP error responses.
//
// As of the error-code-message-standardization effort it no longer emits its
// own bespoke shape ({ "error": { "code": "VALIDATION_ERROR", ... } }). Instead
// it delegates to the unified authforge::common::error machinery so every validation
// failure is rendered as a VALIDATION-class Error Envelope (code
// VALIDATION_INVALID_INPUT, category VALIDATION, HTTP 400). The legacy aliases
// `error_description` / `reason` / `VALIDATION_ERROR` / `timestamp` are gone
// (Requirement 7.4 / 7.5). The Production_Mode decision and the field-level
// `details` are gated by authforge::common::error::ErrorContext (Requirement 7.6).
//
// The public method signatures are intentionally unchanged so existing call
// sites (RequestValidationFilter, OAuth2StandardController, SessionController)
// keep compiling.
class HttpResponder
{
  public:
    static ::drogon::HttpResponsePtr buildErrorResponse(
      const std::vector<std::string> &errors,
      const ::drogon::HttpRequestPtr &req = nullptr
    );
    static void respondWithError(
      const std::string &field,
      const std::string &reason,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const ::drogon::HttpRequestPtr &req = nullptr
    );
    static void respondWithErrors(
      const std::vector<std::string> &errors,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const ::drogon::HttpRequestPtr &req = nullptr
    );
    static bool respondIfErrors(
      const std::vector<std::string> &errors,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const ::drogon::HttpRequestPtr &req = nullptr
    );

  private:
    // Builds the VALIDATION-class Error Envelope JSON for the given validation
    // error strings, delegating to authforge::common::error::Error / ErrorContext.
    static Json::Value buildErrorJson(
      const std::vector<std::string> &errors,
      const ::drogon::HttpRequestPtr &req = nullptr
    );
};

}  // namespace authforge::drogon::validation
