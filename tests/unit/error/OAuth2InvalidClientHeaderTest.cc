#include <drogon/drogon_test.h>
#include <drogon/HttpResponse.h>
#include <fulla/drogon/error/OAuth2ErrorHandler.h>

#include <string>

using namespace drogon;
using namespace fulla::common::error;

// Feature: error-code-message-standardization
// Example/boundary test (specific cases, not a randomized property test) for
// Requirement 2.9 (with 2.4): when an OAuth2_Protocol_Endpoint returns
// `invalid_client` AND the client authenticated via the `Authorization` request
// header, OAuth2ErrorHandler::sendErrorResponse MUST set a `WWW-Authenticate`
// challenge header matching the authentication scheme used, while preserving the
// HTTP 401 status code. For any other situation (empty authScheme, or a
// non-invalid_client code) NO `WWW-Authenticate` header is set.
//
// The HttpResponsePtr is captured via the lambda callback passed to
// sendErrorResponse, then its WWW-Authenticate header and status code are
// inspected. Drogon's HttpResponse::getHeader returns an empty string when the
// header is absent, which is how "no header set" is asserted.

namespace
{

// Invoke sendErrorResponse synchronously and return the captured response.
HttpResponsePtr capture(
  const std::string &errorCode,
  const std::string &authScheme,
  const std::string &description = "",
  const std::string &errorUri = ""
)
{
    HttpResponsePtr captured;
    OAuth2ErrorHandler::sendErrorResponse(
      [&captured](const HttpResponsePtr &resp) { captured = resp; },
      errorCode,
      description,
      errorUri,
      authScheme
    );
    return captured;
}

}  // namespace

// --- Requirement 2.9 / 2.4: invalid_client + "Basic" scheme -> Basic challenge,
// HTTP 401 preserved.
DROGON_TEST(Unit_P0_OAuth2InvalidClient_BasicScheme_SetsMatchingChallengeAnd401)
{
    auto resp = capture(OAuth2ErrorHandler::INVALID_CLIENT, "Basic");
    REQUIRE(resp != nullptr);

    const std::string &challenge = resp->getHeader("WWW-Authenticate");
    // The challenge header is present and matches the scheme the client used.
    CHECK(!challenge.empty());
    CHECK(challenge.rfind("Basic", 0) == 0);  // starts with "Basic"

    // Requirement 2.4: invalid_client preserves HTTP 401.
    CHECK(resp->getStatusCode() == k401Unauthorized);
}

// --- Requirement 2.9 / 2.4: invalid_client + "Bearer" scheme -> Bearer challenge,
// HTTP 401 preserved.
DROGON_TEST(Unit_P0_OAuth2InvalidClient_BearerScheme_SetsMatchingChallengeAnd401)
{
    auto resp = capture(OAuth2ErrorHandler::INVALID_CLIENT, "Bearer");
    REQUIRE(resp != nullptr);

    const std::string &challenge = resp->getHeader("WWW-Authenticate");
    CHECK(!challenge.empty());
    CHECK(challenge.rfind("Bearer", 0) == 0);  // starts with "Bearer"

    CHECK(resp->getStatusCode() == k401Unauthorized);
}

// --- Requirement 2.9 / 2.4: invalid_client WITHOUT an Authorization-header scheme
// (empty authScheme) -> NO WWW-Authenticate header, but HTTP 401 is still kept.
DROGON_TEST(Unit_P0_OAuth2InvalidClient_EmptyScheme_NoChallengeStill401)
{
    auto resp = capture(OAuth2ErrorHandler::INVALID_CLIENT, /*authScheme=*/"");
    REQUIRE(resp != nullptr);

    // The client did not authenticate via the Authorization header, so no
    // challenge header is emitted (the conditional guard requires a non-empty
    // scheme).
    const std::string &challenge = resp->getHeader("WWW-Authenticate");
    CHECK(challenge.empty());

    // Requirement 2.4: invalid_client is still HTTP 401 regardless of the header.
    CHECK(resp->getStatusCode() == k401Unauthorized);
}

// --- Requirement 2.9 boundary: only `invalid_client` triggers the challenge. A
// different protocol code (invalid_request) with a scheme present MUST NOT set a
// WWW-Authenticate header, and its status follows the catalog (400).
DROGON_TEST(Unit_P0_OAuth2InvalidRequest_WithScheme_NoChallenge400)
{
    auto resp = capture(OAuth2ErrorHandler::INVALID_REQUEST, "Basic");
    REQUIRE(resp != nullptr);

    // Only invalid_client emits the challenge header; invalid_request does not.
    const std::string &challenge = resp->getHeader("WWW-Authenticate");
    CHECK(challenge.empty());

    // invalid_request maps to HTTP 400 per the catalog.
    CHECK(resp->getStatusCode() == k400BadRequest);
}
