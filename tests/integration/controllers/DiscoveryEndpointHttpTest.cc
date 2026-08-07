// tests/integration/controllers/DiscoveryEndpointHttpTest.cc
//
// HTTP integration tests for the OIDC/OAuth2 discovery endpoints
// (libs/drogon/src/controllers/DiscoveryController.cc, 259 LOC). These are
// MEMORY-SAFE and unauthenticated -- they run in every CI leg.
//
// Route map (DiscoveryController.h):
//   GET /.well-known/openid-configuration   -> openidConfiguration (OIDC metadata)
//   GET /.well-known/oauth-authorization-server -> oauthAuthorizationServer (RFC 8414)
//   GET /.well-known/jwks.json              -> jwks (JWK Set)

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include "HttpTestClient.h"

#include <string>

using authforge::test::http::parseJsonBody;
using authforge::test::http::sendGet;
using authforge::test::http::statusIs;

// OIDC discovery metadata: assert the RFC 8414 / OIDC required fields are
// present and well-formed. Covers the openidConfiguration happy path.
DROGON_TEST(Integration_P0_Discovery_OpenIdConfiguration_ReturnsRequiredFields)
{
    auto resp = sendGet("/.well-known/openid-configuration");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    // Required OIDC discovery fields.
    CHECK(body.isMember("issuer"));
    CHECK(body.isMember("authorization_endpoint"));
    CHECK(body.isMember("token_endpoint"));
    CHECK(body.isMember("jwks_uri"));
    CHECK(body.isMember("response_types_supported"));
    CHECK(body.isMember("grant_types_supported"));
    CHECK(body.isMember("subject_types_supported"));
    CHECK(body.isMember("id_token_signing_alg_values_supported"));
    // grant_types_supported must include the core grant types.
    const auto grants = body["grant_types_supported"];
    CHECK(grants.isArray());
    bool hasAuthCode = false;
    for (const auto &g : grants)
    {
        if (g.asString() == "authorization_code")
            hasAuthCode = true;
    }
    CHECK(hasAuthCode);
}

// RFC 8414 oauth-authorization-server metadata: same shape family. Covers
// the oauthAuthorizationServer handler (a separate code path that builds a
// similar but RFC-8414-shaped document).
DROGON_TEST(Integration_P0_Discovery_OAuthAuthorizationServer_ReturnsRequiredFields)
{
    auto resp = sendGet("/.well-known/oauth-authorization-server");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body.isMember("issuer"));
    CHECK(body.isMember("authorization_endpoint"));
    CHECK(body.isMember("token_endpoint"));
    // RFC 8414 requires grant_types_supported.
    CHECK(body.isMember("grant_types_supported"));
}

// JWKS endpoint: returns a JWK Set with at least one key. Covers the jwks
// handler, which serializes the current signing keys.
DROGON_TEST(Integration_P0_Discovery_Jwks_ReturnsKeySet)
{
    auto resp = sendGet("/.well-known/jwks.json");
    REQUIRE(resp != nullptr);
    CHECK(statusIs(resp, drogon::k200OK));
    Json::Value body;
    REQUIRE(parseJsonBody(resp, body));
    CHECK(body.isMember("keys"));
    CHECK(body["keys"].isArray());
    CHECK(body["keys"].size() >= 1);
    // Each key should carry kty + kid at minimum.
    const auto firstKey = body["keys"][0];
    CHECK(firstKey.isMember("kty"));
    CHECK(firstKey.isMember("kid"));
}
