#include <drogon/drogon_test.h>

#include <authforge/drogon/authz/ResourceScopeRegistry.h>
#include <authforge/drogon/authz/ScopeResolver.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>

// Unit tests for the resource-scope authorization model (#43):
//  - ScopeResolver: exact / implied / OIDC-non-implication / All / Any /
//    empty-match semantics, plus the RFC 6750 §3 multi-scope challenge.
//  - ResourceScopeRegistry: template path matching (concrete vs {param}).

DROGON_TEST(Unit_P0_Authz_ScopeResolver_ExactMatch)
{
    authforge::drogon::authz::ResourceScopeRequirement req;
    req.scopes = {"users:read"};
    req.match = authforge::drogon::authz::ScopeMatch::All;

    CHECK(authforge::drogon::authz::satisfies("openid users:read profile", req) == true);
    CHECK(authforge::drogon::authz::satisfies("openid profile", req) == false);
}

DROGON_TEST(Unit_P0_Authz_ScopeResolver_Implication)
{
    // admin super-scope satisfies users:read via impliedBy.
    authforge::drogon::authz::ResourceScopeRequirement req;
    req.scopes = {"users:read"};
    req.impliedBy = {"admin"};

    // Token carries only "admin" -> satisfied by implication.
    CHECK(authforge::drogon::authz::satisfies("admin", req) == true);
    // Token carries the exact scope -> satisfied.
    CHECK(authforge::drogon::authz::satisfies("users:read", req) == true);
    // Token carries neither -> not satisfied.
    CHECK(authforge::drogon::authz::satisfies("openid profile", req) == false);
}

DROGON_TEST(Unit_P0_Authz_ScopeResolver_OIDC_NoImplication)
{
    // RFC 6749 §3.3 / OIDC Core §5.4: the standard scopes are NEVER implied
    // by admin. A userinfo request requires an actual user token carrying
    // "openid". The requirement carries an empty impliedBy.
    authforge::drogon::authz::ResourceScopeRequirement req;
    req.scopes = {"openid"};
    req.impliedBy = {};  // explicitly none

    CHECK(authforge::drogon::authz::satisfies("admin", req) == false);
    CHECK(authforge::drogon::authz::satisfies("openid", req) == true);
}

DROGON_TEST(Unit_P0_Authz_ScopeResolver_AllMatch)
{
    authforge::drogon::authz::ResourceScopeRequirement req;
    req.scopes = {"users:read", "clients:read"};
    req.match = authforge::drogon::authz::ScopeMatch::All;
    req.impliedBy = {"admin"};

    // All present exactly.
    CHECK(authforge::drogon::authz::satisfies("users:read clients:read", req) == true);
    // All satisfied via the single admin super-scope.
    CHECK(authforge::drogon::authz::satisfies("admin", req) == true);
    // Only one of two -> not satisfied under All.
    CHECK(authforge::drogon::authz::satisfies("users:read", req) == false);
}

DROGON_TEST(Unit_P0_Authz_ScopeResolver_AnyMatch)
{
    authforge::drogon::authz::ResourceScopeRequirement req;
    req.scopes = {"users:read", "clients:read"};
    req.match = authforge::drogon::authz::ScopeMatch::Any;
    req.impliedBy = {"admin"};

    // One of two is enough under Any.
    CHECK(authforge::drogon::authz::satisfies("users:read", req) == true);
    // admin implies both -> satisfied.
    CHECK(authforge::drogon::authz::satisfies("admin", req) == true);
    // Neither -> not satisfied.
    CHECK(authforge::drogon::authz::satisfies("openid profile", req) == false);
}

DROGON_TEST(Unit_P0_Authz_ScopeResolver_EmptyRequirement)
{
    // An empty scopes list is vacuously satisfied (no requirement). This is
    // the "public / unconfigured" case; lookup() returns nullptr for such
    // routes, but satisfies() itself must be total.
    authforge::drogon::authz::ResourceScopeRequirement req;
    CHECK(authforge::drogon::authz::satisfies("", req) == true);
    CHECK(authforge::drogon::authz::satisfies("anything", req) == true);
}

DROGON_TEST(Unit_P0_Authz_Challenge_SingleScope)
{
    authforge::drogon::authz::ResourceScopeRequirement req;
    req.scopes = {"openid"};

    const std::string challenge = authforge::drogon::authz::buildInsufficientScopeChallenge(req);
    // RFC 6750 §3: error="insufficient_scope" + scope attribute present.
    CHECK(challenge.find("error=\"insufficient_scope\"") != std::string::npos);
    CHECK(challenge.find("scope=\"openid\"") != std::string::npos);
    CHECK(challenge.find("realm=\"authforge\"") != std::string::npos);
}

DROGON_TEST(Unit_P0_Authz_Challenge_MultiScope_SpaceDelimited)
{
    // R1: a multi-scope requirement emits a SPACE-DELIMITED scope attribute
    // (RFC 6750 §3), not a single token.
    authforge::drogon::authz::ResourceScopeRequirement req;
    req.scopes = {"users:read", "clients:write"};

    const std::string challenge = authforge::drogon::authz::buildInsufficientScopeChallenge(req);
    CHECK(challenge.find("scope=\"users:read clients:write\"") != std::string::npos);
}

// ===========================================================================
// M4: ResourceScopeRegistry unit tests.
// Uses registerPrefix with ISOLATED test-only prefixes (e.g. /test/authz/*)
// that never collide with real routes. Does NOT call clear() or
// buildFromEndpoints() -- those mutate the global registry and would break
// subsequent integration tests (OAuth2Tests) that depend on the real
// registry built at startup.
// ===========================================================================

DROGON_TEST(Unit_P0_Authz_Registry_PrefixFallback)
{
    // Prefix: any subpath of /test/authz/unit should match.
    authforge::drogon::authz::ResourceScopeRequirement testReq;
    testReq.scopes = {"test:scope"};
    testReq.impliedBy = {"admin"};
    authforge::drogon::authz::ResourceScopeRegistry::registerPrefix(
      "/test/authz/unit", testReq);

    // Direct prefix path matches.
    const auto *direct = authforge::drogon::authz::ResourceScopeRegistry::lookup(
      "/test/authz/unit", drogon::Get);
    REQUIRE(direct != nullptr);
    CHECK(direct->scopes == std::vector<std::string>{"test:scope"});

    // Subpath matches via prefix fallback.
    const auto *sub = authforge::drogon::authz::ResourceScopeRegistry::lookup(
      "/test/authz/unit/deep/nested", drogon::Post);
    REQUIRE(sub != nullptr);
    CHECK(sub->impliedBy == std::vector<std::string>{"admin"});

    // Boundary check: /test/authz/other must NOT match /test/authz/unit.
    CHECK(authforge::drogon::authz::ResourceScopeRegistry::lookup(
            "/test/authz/other", drogon::Get) == nullptr);

    // Completely unrelated path -> nullptr.
    CHECK(authforge::drogon::authz::ResourceScopeRegistry::lookup(
            "/totally/unrelated", drogon::Get) == nullptr);

    // Cleanup: remove test-only prefixes so subsequent integration tests
    // see a clean prefix set (exact entries from buildFromEndpoints stay).
    authforge::drogon::authz::ResourceScopeRegistry::clearPrefixes();
}

DROGON_TEST(Unit_P0_Authz_Registry_LongestPrefixWins)
{
    // Register a shorter and a longer prefix under the same hierarchy.
    authforge::drogon::authz::ResourceScopeRequirement shortReq;
    shortReq.scopes = {"short"};
    authforge::drogon::authz::ResourceScopeRequirement longReq;
    longReq.scopes = {"long"};

    authforge::drogon::authz::ResourceScopeRegistry::registerPrefix(
      "/test/authz/long", longReq);
    authforge::drogon::authz::ResourceScopeRegistry::registerPrefix(
      "/test/authz", shortReq);

    // /test/authz/long/sub should match the LONGER prefix (more specific).
    const auto *best = authforge::drogon::authz::ResourceScopeRegistry::lookup(
      "/test/authz/long/sub", drogon::Get);
    REQUIRE(best != nullptr);
    CHECK(best->scopes == std::vector<std::string>{"long"});

    // /test/authz/other should match the SHORTER prefix.
    const auto *shorter = authforge::drogon::authz::ResourceScopeRegistry::lookup(
      "/test/authz/other", drogon::Get);
    REQUIRE(shorter != nullptr);
    CHECK(shorter->scopes == std::vector<std::string>{"short"});

    authforge::drogon::authz::ResourceScopeRegistry::clearPrefixes();
}
