// Feature: error-code-message-standardization — validation Error Envelope tests.
//
// Example/unit tests (specific cases, not randomized property tests) for task
// 6.2, covering Requirements 7.4 and 7.6:
//
//   * Requirement 7.4: a field-level validation failure is rendered as a
//     VALIDATION-class Error Envelope (code VALIDATION_INVALID_INPUT, category
//     VALIDATION, numeric_code 3001) with HTTP_Status_Code 400 and
//     Content-Type application/json.
//   * Requirement 7.6 (with 5.1): in Production_Mode the `details` key is fully
//     absent; in non-Production_Mode `details` is present and lists the field
//     names and the corresponding failure reasons.
//
// fulla::drogon::validation::HttpResponder::buildErrorResponse is the entry point under
// test; it builds the Envelope via fulla::common::error::Error and gates `details` on
// fulla::common::error::ErrorContext. The ErrorContext test hooks
// (setDetailedErrorsOverride / clearDetailedErrorsOverride) switch
// Production_Mode deterministically without relying on build flags or
// environment variables. Every test restores the default state with
// clearDetailedErrorsOverride() so it does not bleed into other tests.
//
// Backend tests use Drogon's drogon_test.h (DROGON_TEST macro). Because
// test/CMakeLists.txt collects unit/*.cc with GLOB_RECURSE, this file is picked
// up automatically with no CMake changes.

#include <drogon/drogon_test.h>
#include <drogon/HttpResponse.h>
#include <fulla/drogon/validation/HttpResponder.h>
#include <fulla/common/error/ErrorContext.h>
#include <json/json.h>

#include <memory>
#include <string>
#include <vector>

using namespace drogon;
using namespace fulla::common::error;
using fulla::drogon::validation::HttpResponder;

namespace
{

// Parse the response body as JSON (same convention as the error property
// tests). Returns false on parse failure.
bool parseBody(const HttpResponsePtr &resp, Json::Value &out)
{
    const std::string body(resp->getBody());
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    return reader->parse(body.data(), body.data() + body.size(), &out, &errs);
}

// The field-level validation errors used across the tests. Each entry embeds a
// field name and the failure reason (the shape produced by respondWithError /
// the validation filter).
const std::vector<std::string> &sampleErrors()
{
    static const std::vector<std::string> kErrors = {
      "username: required",
      "email: invalid format",
    };
    return kErrors;
}

}  // namespace

// --- Requirement 7.4: VALIDATION Envelope, HTTP 400, application/json. --------
// buildErrorResponse must produce an HTTP 400 response whose Content-Type is
// application/json, regardless of the Production_Mode setting.
DROGON_TEST(Unit_P0_ValidationEnvelope_Status400AndJsonContentType)
{
    // Make the mode deterministic; the status/content-type are mode-independent.
    ErrorContext::setDetailedErrorsOverride(true);

    auto resp = HttpResponder::buildErrorResponse(sampleErrors());
    REQUIRE(resp != nullptr);

    // HTTP 400 for the VALIDATION category (Requirement 7.4 / 4.1).
    CHECK(resp->getStatusCode() == k400BadRequest);

    // Content-Type: application/json (Requirement 1.4 / 7.4). Drogon stores the
    // typed content type separately from the generic header map.
    CHECK(resp->contentType() == CT_APPLICATION_JSON);

    ErrorContext::clearDetailedErrorsOverride();
}

// --- Requirement 7.4 / 7.5: VALIDATION Error Envelope shape, no legacy aliases.
// The body is a top-level single `error` object with the VALIDATION code,
// category, numeric_code 3001 and a non-empty request_id; no legacy aliases
// (error_description / reason / VALIDATION_ERROR / timestamp) are present.
DROGON_TEST(Unit_P0_ValidationEnvelope_EnvelopeShapeAndNoLegacyAliases)
{
    ErrorContext::setDetailedErrorsOverride(true);

    auto resp = HttpResponder::buildErrorResponse(sampleErrors());
    REQUIRE(resp != nullptr);

    Json::Value root;
    REQUIRE(parseBody(resp, root));

    // Top-level: a single `error` object and nothing else (Requirement 1.1).
    REQUIRE(root.isObject());
    REQUIRE(root.isMember("error"));
    CHECK(root.getMemberNames().size() == 1u);
    // No legacy top-level aliases.
    CHECK(!root.isMember("error_description"));
    CHECK(!root.isMember("reason"));
    CHECK(!root.isMember("timestamp"));

    const Json::Value &err = root["error"];
    REQUIRE(err.isObject());

    // VALIDATION-class identity (Requirement 7.4).
    REQUIRE(err.isMember("code"));
    CHECK(err["code"].asString() == "VALIDATION_INVALID_INPUT");
    REQUIRE(err.isMember("category"));
    CHECK(err["category"].asString() == "VALIDATION");

    // numeric_code present and equal to the catalog value 3001 (Requirement 1.3).
    REQUIRE(err.isMember("numeric_code"));
    CHECK(err["numeric_code"].asInt() == 3001);

    // Non-empty request_id (Requirement 6.1).
    REQUIRE(err.isMember("request_id"));
    CHECK(!err["request_id"].asString().empty());

    // message present and non-empty (Client_Safe_Message).
    REQUIRE(err.isMember("message"));
    CHECK(!err["message"].asString().empty());

    // No legacy aliases inside the error object either (Requirement 7.5): the
    // old VALIDATION_ERROR shape used `reason` / `error_description` / `timestamp`.
    CHECK(!err.isMember("error_description"));
    CHECK(!err.isMember("reason"));
    CHECK(!err.isMember("timestamp"));
    // code must not be the retired alias value.
    CHECK(err["code"].asString() != "VALIDATION_ERROR");

    ErrorContext::clearDetailedErrorsOverride();
}

// --- Requirement 5.1 / 7.6: Production_Mode hides `details` entirely. ----------
// With detailed errors disabled (Production_Mode) the `details` key must be
// fully absent from the Envelope (not null, not empty string).
DROGON_TEST(Unit_P0_ValidationEnvelope_ProductionModeOmitsDetails)
{
    // setDetailedErrorsOverride(false) simulates Production_Mode.
    ErrorContext::setDetailedErrorsOverride(false);

    auto resp = HttpResponder::buildErrorResponse(sampleErrors());
    REQUIRE(resp != nullptr);

    Json::Value root;
    REQUIRE(parseBody(resp, root));
    REQUIRE(root.isMember("error"));
    const Json::Value &err = root["error"];

    // `details` key is fully omitted in Production_Mode (Requirement 5.1).
    CHECK(!err.isMember("details"));

    // The Envelope identity is still correct in Production_Mode.
    CHECK(err["code"].asString() == "VALIDATION_INVALID_INPUT");
    CHECK(err["category"].asString() == "VALIDATION");
    CHECK(resp->getStatusCode() == k400BadRequest);

    // Restore default state for subsequent tests.
    ErrorContext::clearDetailedErrorsOverride();
}

// --- Requirement 7.6: non-Production_Mode `details` lists field names + reasons.
// With detailed errors enabled (non-Production_Mode) the `details` key is
// present and contains the field names and failure reasons that were passed in.
DROGON_TEST(Unit_P0_ValidationEnvelope_NonProductionDetailsListFieldsAndReasons)
{
    // setDetailedErrorsOverride(true) simulates non-Production_Mode.
    ErrorContext::setDetailedErrorsOverride(true);

    auto resp = HttpResponder::buildErrorResponse(sampleErrors());
    REQUIRE(resp != nullptr);

    Json::Value root;
    REQUIRE(parseBody(resp, root));
    REQUIRE(root.isMember("error"));
    const Json::Value &err = root["error"];

    // `details` present (Requirement 7.6).
    REQUIRE(err.isMember("details"));
    const std::string details = err["details"].asString();
    CHECK(!details.empty());

    // The field names and failure reasons passed in are surfaced in `details`.
    CHECK(details.find("username") != std::string::npos);
    CHECK(details.find("required") != std::string::npos);
    CHECK(details.find("email") != std::string::npos);
    CHECK(details.find("invalid format") != std::string::npos);

    // Restore default state for subsequent tests.
    ErrorContext::clearDetailedErrorsOverride();
}

// --- Requirement 7.4 / 7.6: respondWithError single field convenience path. ----
// The single-field convenience entry point produces the same VALIDATION
// Envelope, and its `details` carries the field name and reason in
// non-Production_Mode.
DROGON_TEST(Unit_P0_ValidationEnvelope_RespondWithErrorSingleField)
{
    ErrorContext::setDetailedErrorsOverride(true);

    HttpResponsePtr captured;
    HttpResponder::respondWithError(
      "password", "too short", [&captured](const HttpResponsePtr &resp) { captured = resp; }
    );
    REQUIRE(captured != nullptr);

    CHECK(captured->getStatusCode() == k400BadRequest);
    CHECK(captured->contentType() == CT_APPLICATION_JSON);

    Json::Value root;
    REQUIRE(parseBody(captured, root));
    REQUIRE(root.isMember("error"));
    const Json::Value &err = root["error"];

    CHECK(err["code"].asString() == "VALIDATION_INVALID_INPUT");
    CHECK(err["category"].asString() == "VALIDATION");

    REQUIRE(err.isMember("details"));
    const std::string details = err["details"].asString();
    CHECK(details.find("password") != std::string::npos);
    CHECK(details.find("too short") != std::string::npos);

    ErrorContext::clearDetailedErrorsOverride();
}
