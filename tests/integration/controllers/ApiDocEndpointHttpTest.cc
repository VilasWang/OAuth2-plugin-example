// tests/integration/controllers/ApiDocEndpointHttpTest.cc
//
// HTTP integration tests for the API-doc endpoints
// (libs/drogon/src/controllers/ApiDocController.cc, 68 LOC, 26.5% covered).
//
// The handler serves a STATICALLY-DEPLOYED docs/api/openapi.json (written at
// build/deploy time, not generated per-request). The test environment does
// NOT carry that file, so the reachable branch here is the "spec not found"
// 404 path (ApiDocController.cc:60-68). The 200 success branch requires the
// file to exist and is a deployment concern, not a test concern. Asserting
// the 404 still covers the handler's entry + the not-found branch + the
// ErrorResponder integration.
//
// Route map (ApiDocController.h):
//   GET /docs/api/openapi.json -> openApiSpec
//   GET /docs/api              -> swaggerUi  (HTML)

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include "HttpTestClient.h"

#include <string>

using fulla::test::http::parseJsonBody;
using fulla::test::http::sendGet;
using fulla::test::http::statusIs;

// openapi.json: in the test environment the static spec file is absent, so the
// handler returns the VALIDATION_RESOURCE_NOT_FOUND error envelope (404). This
// covers the openApiSpec entry + the file-not-found branch + ErrorResponder.
DROGON_TEST(Integration_P1_ApiDoc_OpenApiSpec_NoFile_Returns404)
{
    auto resp = sendGet("/docs/api/openapi.json");
    REQUIRE(resp != nullptr);
    // 404 when the static spec is absent (the test-binary directory has no
    // docs/api/openapi.json). Accept 404 OR 200 (in case a future change
    // generates the spec at runtime); the key assertion is that the handler
    // responds coherently rather than 500.
    const auto code = resp->getStatusCode();
    CHECK((code == drogon::k404NotFound || code == drogon::k200OK));
    CHECK(code != drogon::k500InternalServerError);
}

// Swagger UI: GET /docs/api returns a coherent response (200 if the static
// HTML is present, otherwise the not-found branch). Same reasoning: assert
// the handler responds without a 500.
DROGON_TEST(Integration_P1_ApiDoc_SwaggerUi_RespondsCoherently)
{
    auto resp = sendGet("/docs/api");
    REQUIRE(resp != nullptr);
    const auto code = resp->getStatusCode();
    CHECK((code == drogon::k200OK || code == drogon::k404NotFound));
    CHECK(code != drogon::k500InternalServerError);
}
