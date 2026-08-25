// Coverage additions (P1, fulla coverage push): HttpResponder's
// respondIfErrors decision logic (HttpResponder.cc:115-127) had no direct
// coverage -- it was exercised only indirectly through controllers. These
// pin the short-circuit: an empty error vector returns false and does NOT
// invoke the callback, while a non-empty vector returns true and invokes
// the callback exactly once with a 400 VALIDATION response.

#include <drogon/drogon_test.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <fulla/common/error/ErrorContext.h>
#include <fulla/drogon/validation/HttpResponder.h>
#include <json/json.h>

#include <sstream>
#include <string>
#include <vector>

using namespace drogon;
using fulla::common::error::ErrorContext;
using fulla::drogon::validation::HttpResponder;

namespace
{
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

DROGON_TEST(Unit_P1_Validation_HttpResponder_RespondIfErrors_Empty_DoesNotInvokeCallback)
{
    auto req = HttpRequest::newHttpRequest();
    bool callbackInvoked = false;
    bool handled = HttpResponder::respondIfErrors(
      {}, [&](const HttpResponsePtr &) { callbackInvoked = true; }, req
    );
    CHECK(handled == false);
    CHECK(callbackInvoked == false);
}

DROGON_TEST(Unit_P1_Validation_HttpResponder_RespondIfErrors_NonEmpty_InvokesCallbackOnce)
{
    ErrorContext::setDetailedErrorsOverride(true);
    auto req = HttpRequest::newHttpRequest();
    int invokeCount = 0;
    HttpResponsePtr captured;
    bool handled = HttpResponder::respondIfErrors(
      {"email is invalid", "password too short"},
      [&](const HttpResponsePtr &resp) {
          captured = resp;
          ++invokeCount;
      },
      req
    );
    ErrorContext::clearDetailedErrorsOverride();

    CHECK(handled == true);
    CHECK(invokeCount == 1);
    REQUIRE(captured != nullptr);
    CHECK(captured->getStatusCode() == k400BadRequest);
    CHECK(captured->getContentType() == CT_APPLICATION_JSON);

    auto body = parseBody(captured);
    REQUIRE(body.isMember("error"));
    CHECK(body["error"]["code"].asString() == "VALIDATION_INVALID_INPUT");
}

DROGON_TEST(Unit_P2_Validation_HttpResponder_BuildErrorResponse_NullReq_StillProduces400)
{
    // buildErrorResponse with a null request must still produce a valid
    // VALIDATION envelope (the null-req branch resolves a request_id itself).
    ErrorContext::setDetailedErrorsOverride(true);
    auto resp = HttpResponder::buildErrorResponse({"field is bad"}, nullptr);
    ErrorContext::clearDetailedErrorsOverride();

    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k400BadRequest);
    auto body = parseBody(resp);
    REQUIRE(body.isMember("error"));
    CHECK(body["error"]["code"].asString() == "VALIDATION_INVALID_INPUT");
}
