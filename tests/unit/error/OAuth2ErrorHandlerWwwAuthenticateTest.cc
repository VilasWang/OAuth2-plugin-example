// Coverage additions (P1, authforge coverage push): the OAuth2ErrorHandler
// WWW-Authenticate branch (OAuth2ErrorHandler.cc:85-88) and the unknown-code
// getHttpStatusCode fallback (cc:106) had no targeted coverage. These pin:
//   * invalid_client + a non-empty authScheme adds a WWW-Authenticate header
//     with the scheme + realm (RFC 6749 §5.2),
//   * an empty authScheme (or a non-invalid_client code) adds NO such header,
//   * getHttpStatusCode for an unknown code falls back to k400BadRequest.

#include <drogon/drogon_test.h>
#include <drogon/HttpResponse.h>
#include <authforge/common/error/ErrorCatalog.h>
#include <authforge/drogon/error/OAuth2ErrorHandler.h>

#include <string>

using namespace drogon;
using namespace authforge::common::error;

namespace
{
HttpResponsePtr capture(
  const std::string &errorCode,
  const std::string &description = "",
  const std::string &errorUri = "",
  const std::string &authScheme = ""
)
{
    HttpResponsePtr captured;
    OAuth2ErrorHandler::sendErrorResponse(
      [&captured](const HttpResponsePtr &resp) { captured = resp; }, errorCode, description,
      errorUri, authScheme
    );
    return captured;
}
}  // namespace

DROGON_TEST(Unit_P1_Error_OAuth2ErrorHandler_InvalidClientWithAuthScheme_AddsWwwAuthenticate)
{
    auto resp = capture(OAuth2ErrorHandler::INVALID_CLIENT, "bad creds", "", "Basic");
    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k401Unauthorized);
    // WWW-Authenticate carries the scheme + the documented realm.
    auto www = resp->getHeader("WWW-Authenticate");
    CHECK(www.find("Basic") != std::string::npos);
    CHECK(www.find("realm=\"OAuth2 Client Authentication\"") != std::string::npos);
}

DROGON_TEST(Unit_P1_Error_OAuth2ErrorHandler_InvalidClientNoAuthScheme_NoWwwAuthenticate)
{
    // invalid_client but EMPTY authScheme -> no WWW-Authenticate header added.
    auto resp = capture(OAuth2ErrorHandler::INVALID_CLIENT, "bad creds", "", "");
    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k401Unauthorized);
    CHECK(resp->getHeader("WWW-Authenticate").empty());
}

DROGON_TEST(Unit_P2_Error_OAuth2ErrorHandler_NonInvalidClientCode_NoWwwAuthenticate)
{
    // A non-invalid_client code never adds WWW-Authenticate even with a scheme.
    auto resp = capture("invalid_grant", "bad grant", "", "Basic");
    REQUIRE(resp != nullptr);
    CHECK(resp->getHeader("WWW-Authenticate").empty());
}

DROGON_TEST(Unit_P1_Error_OAuth2ErrorHandler_GetHttpStatusCode_UnknownCode_FallsBackTo400)
{
    // An unregistered protocol code falls back to 400 Bad Request
    // (OAuth2ErrorHandler.cc:106).
    CHECK(OAuth2ErrorHandler::getHttpStatusCode("not_a_real_protocol_code") == k400BadRequest);
}

DROGON_TEST(Unit_P2_Error_OAuth2ErrorHandler_GetHttpStatusCode_KnownCodes_Mapped)
{
    // Spot-check a few catalog-registered mappings (Requirement 2.7).
    CHECK(OAuth2ErrorHandler::getHttpStatusCode(OAuth2ErrorHandler::INVALID_CLIENT) == k401Unauthorized);
    // invalid_grant / invalid_request are 400.
    CHECK(OAuth2ErrorHandler::getHttpStatusCode("invalid_grant") == k400BadRequest);
    CHECK(OAuth2ErrorHandler::getHttpStatusCode("invalid_request") == k400BadRequest);
}
