#pragma once

// M3 Task 20: relocated verbatim from
// OAuth2Plugin/include/oauth2/validation/HttpResponder.h into
// authforge::drogon::validation (see Rules.h in this directory for the
// migration rationale). Still depends on the pre-existing common::error
// machinery (OAuth2Plugin/include/oauth2/error/*) -- that namespace's own
// relocation into authforge::common::error (already partially done, see
// libs/common/include/authforge/common/error/) is a separate, later
// slice; this migration keeps calling the existing common::error:: call
// sites unchanged.

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
// It delegates to the unified common::error machinery so every validation
// failure is rendered as a VALIDATION-class Error Envelope (code
// VALIDATION_INVALID_INPUT, category VALIDATION, HTTP 400).
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
    // error strings, delegating to common::error::Error / ErrorContext.
    static Json::Value buildErrorJson(
      const std::vector<std::string> &errors,
      const ::drogon::HttpRequestPtr &req = nullptr
    );
};

}  // namespace authforge::drogon::validation
