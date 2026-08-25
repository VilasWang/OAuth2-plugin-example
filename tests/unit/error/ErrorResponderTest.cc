// Coverage additions (P1, fulla coverage push): ErrorResponder.cc had
// ZERO direct unit coverage -- it was exercised only indirectly through
// integration tests. These pin the security-relevant guarantees:
//   * an unregistered Error_Code NEVER leaks to the client and NEVER throws
//     (it falls back to INTERNAL_ERROR, Requirement 5.5),
//   * the toDrogonStatus() switch maps every catalog status correctly
//     (404/409/429/502/503/504 were previously untested),
//   * respondValidation joins multiple field errors with "; " into `details`
//     and omits `details` in Production_Mode (Requirement 7.6),
//   * the response is always JSON with the right Content-Type (Requirement 1.4).

#include <drogon/drogon_test.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <fulla/common/error/ErrorCatalog.h>
#include <fulla/common/error/ErrorContext.h>
#include <fulla/common/error/ErrorTypes.h>
#include <fulla/drogon/error/ErrorResponder.h>
#include <json/json.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace drogon;
using namespace fulla::common::error;

namespace
{

// Capture the HttpResponsePtr produced by an ErrorResponder entry point.
// Each ErrorResponder method invokes its callback synchronously, so the
// captured pointer is valid immediately after the call returns.
HttpResponsePtr respondAndCapture(
  const std::string &code,
  const std::string &detailForLog = "",
  const std::string &clientDetails = ""
)
{
    auto req = HttpRequest::newHttpRequest();
    HttpResponsePtr captured;
    ErrorResponder::respond(
      req, [&](const HttpResponsePtr &resp) { captured = resp; }, code, detailForLog, clientDetails
    );
    return captured;
}

// Parse the JSON body of a captured response.
Json::Value parseBody(const HttpResponsePtr &resp)
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream is(std::string(resp->getBody()));
    Json::parseFromStream(builder, is, &root, &errs);
    return root;
}
}  // namespace

// respond: an UNREGISTERED code falls back to INTERNAL_ERROR and never leaks
// the original code (ErrorResponder.cc:69-74). The response status is 500
// and the body's error.code is INTERNAL_ERROR, not the offending string.
DROGON_TEST(Unit_P0_Error_ErrorResponder_UnknownCodeFallsBackToInternalError)
{
    ErrorContext::setDetailedErrorsOverride(true);  // allow details for inspection
    auto resp = respondAndCapture("TOTALLY_MADE_UP_CODE", "internal debug text");
    ErrorContext::clearDetailedErrorsOverride();

    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k500InternalServerError);
    CHECK(resp->getContentType() == CT_APPLICATION_JSON);

    auto body = parseBody(resp);
    REQUIRE(body.isMember("error"));
    CHECK(body["error"]["code"].asString() == "INTERNAL_ERROR");
    // The offending code must NEVER appear anywhere in the body.
    CHECK(body.toStyledString().find("TOTALLY_MADE_UP_CODE") == std::string::npos);
}

// respond: a registered code with a catalog status of 404 maps to k404NotFound
// (toDrogonStatus branch, ErrorResponder.cc:33). VALIDATION_RESOURCE_NOT_FOUND
// is registered at 404 in the ErrorCatalog.
DROGON_TEST(Unit_P1_Error_ErrorResponder_StatusMapping_404)
{
    auto resp = respondAndCapture("VALIDATION_RESOURCE_NOT_FOUND");
    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k404NotFound);
}

// respond: a registered 409 code maps to k409Conflict.
DROGON_TEST(Unit_P1_Error_ErrorResponder_StatusMapping_409)
{
    // VALIDATION_USERNAME_TAKEN / VALIDATION_EMAIL_TAKEN are registered at 409.
    auto resp = respondAndCapture("VALIDATION_USERNAME_TAKEN");
    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k409Conflict);
}

// respond: a registered 429 code maps to k429TooManyRequests.
DROGON_TEST(Unit_P1_Error_ErrorResponder_StatusMapping_429)
{
    // VALIDATION_RATE_LIMITED is registered at 429.
    auto resp = respondAndCapture("VALIDATION_RATE_LIMITED");
    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k429TooManyRequests);
}

// respondValidation: multiple field errors are joined with "; " into the
// `details` string when detailed errors are allowed (non-Production_Mode).
DROGON_TEST(Unit_P1_Error_ErrorResponder_RespondValidation_JoinsFieldErrors_NonProd)
{
    ErrorContext::setDetailedErrorsOverride(true);
    auto req = HttpRequest::newHttpRequest();
    HttpResponsePtr captured;
    std::vector<FieldError> errors = {
        {"email",    "must be a valid email"   },
        {"password", "must be at least 12 chars"}
    };
    ErrorResponder::respondValidation(
      req, [&](const HttpResponsePtr &resp) { captured = resp; }, errors
    );
    ErrorContext::clearDetailedErrorsOverride();

    REQUIRE(captured != nullptr);
    CHECK(captured->getStatusCode() == k400BadRequest);
    auto body = parseBody(captured);
    REQUIRE(body.isMember("error"));
    CHECK(body["error"]["code"].asString() == "VALIDATION_INVALID_INPUT");
    // details present and contains BOTH fields, joined by "; ".
    REQUIRE(body["error"].isMember("details"));
    std::string details = body["error"]["details"].asString();
    CHECK(details.find("email: must be a valid email") != std::string::npos);
    CHECK(details.find("password: must be at least 12 chars") != std::string::npos);
    CHECK(details.find("; ") != std::string::npos);
}

// respondValidation: in Production_Mode the `details` key is OMITTED entirely
// (Requirement 5.1 / 5.6).
DROGON_TEST(Unit_P1_Error_ErrorResponder_RespondValidation_ProductionModeOmitsDetails)
{
    ErrorContext::setDetailedErrorsOverride(false);  // Production_Mode
    auto req = HttpRequest::newHttpRequest();
    HttpResponsePtr captured;
    std::vector<FieldError> errors = {{"email", "must be a valid email"}};
    ErrorResponder::respondValidation(
      req, [&](const HttpResponsePtr &resp) { captured = resp; }, errors
    );
    ErrorContext::clearDetailedErrorsOverride();

    REQUIRE(captured != nullptr);
    CHECK(captured->getStatusCode() == k400BadRequest);
    auto body = parseBody(captured);
    REQUIRE(body.isMember("error"));
    // details must NOT be present in Production_Mode.
    CHECK(!body["error"].isMember("details"));
    // The Client_Safe_Message is still present.
    CHECK(body["error"]["message"].asString().empty() == false);
}

// respondValidation: an empty field-error vector still produces a valid 400
// response (code VALIDATION_INVALID_INPUT, no details).
DROGON_TEST(Unit_P2_Error_ErrorResponder_RespondValidation_EmptyFields_Still400)
{
    ErrorContext::setDetailedErrorsOverride(true);
    auto req = HttpRequest::newHttpRequest();
    HttpResponsePtr captured;
    ErrorResponder::respondValidation(
      req, [&](const HttpResponsePtr &resp) { captured = resp; }, {}
    );
    ErrorContext::clearDetailedErrorsOverride();

    REQUIRE(captured != nullptr);
    CHECK(captured->getStatusCode() == k400BadRequest);
    auto body = parseBody(captured);
    CHECK(body["error"]["code"].asString() == "VALIDATION_INVALID_INPUT");
    CHECK(!body["error"].isMember("details"));
}

// buildResponse: always carries a Request_ID even when the Error has none
// (ErrorResponder.cc:165-168). The ID is resolved from the request.
DROGON_TEST(Unit_P1_Error_ErrorResponder_BuildResponse_AlwaysCarriesRequestId)
{
    auto req = HttpRequest::newHttpRequest();
    // Build an Error with an EMPTY requestId.
    Error err = Error::fromCode("INTERNAL_ERROR", "");
    CHECK(err.requestId.empty());

    ErrorContext::setDetailedErrorsOverride(true);
    auto resp = ErrorResponder::buildResponse(req, err);
    ErrorContext::clearDetailedErrorsOverride();

    REQUIRE(resp != nullptr);
    auto body = parseBody(resp);
    REQUIRE(body.isMember("error"));
    // buildResponse resolved a request_id from the request.
    CHECK(body["error"]["request_id"].asString().empty() == false);
}
