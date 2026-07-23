// OAuth2Server/test/unit/initorder/CategoryA_InitOrderSnapshotTest.cc
//
// Spec: concurrency-lifetime-safety-audit — Task 1 (Category A reproduction).
// Property 1: Bug Condition — Init-Order Safety (SIOF) — covers 1.1 / 1.2.
//
// METHODOLOGY (read carefully):
//   These are EXPLORATORY bug-condition + PRESERVATION-BASELINE tests written
//   and run on the UNFIXED code (F). Category A defects (SIOF / cross-TU global
//   ctor order) are intrinsically link-order / platform dependent and do NOT
//   deterministically crash in a single in-process build. Per the task, we use
//   a DETERMINISTIC, VERIFIABLE SUBSTITUTE:
//
//     1.1  Snapshot the OpenApi document (endpoint set / params / response
//          examples) and assert completeness. This snapshot establishes the
//          baseline that later preservation checks (task 6.4) diff against.
//          A counterexample for the real SIOF would be a MISSING endpoint when
//          the global ctor of `docs_` runs before the OpenApiGenerator
//          registry is ready under a hostile link order. We document that
//          fragility; the deterministic in-process run is expected to PASS.
//
//     1.2  Assert RequestValidationFilter returns the complete rule set per
//          path. The rule map is a file-scope non-trivial global filled lazily
//          via call_once (the SIOF root cause). We exercise it behaviorally
//          through the public doFilter() interface (the map + getValidationRules
//          are private), establishing the baseline pass/reject behavior.
//
//   These tests are EXPECTED TO PASS on the unfixed code — they capture the
//   correct current behavior as a comparable baseline. The SIOF counterexample
//   is link-order dependent and recorded as an observation, not fabricated.
//
// _Requirements: 2.1, 2.2, 2.3_  (design Property 1)

#include <drogon/drogon_test.h>
#include <drogon/HttpRequest.h>
#include <json/json.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <oauth2/filters/RequestValidationFilter.h>

using namespace authforge::drogon::observability::openapi;

namespace
{
// The endpoint set that OAuth2StandardController::initApiDocs() must register.
// This is the BASELINE contract for 1.1. If a hostile link/init order ever
// caused `docs_`'s ctor to run before the OpenApiGenerator registry was ready,
// one or more of these would be MISSING from the generated spec — that is the
// SIOF counterexample (endpoint missing). On a normal in-process build the
// registration is complete, so this baseline PASSES.
const std::vector<std::pair<std::string, std::string>> &expectedEndpoints()
{
    // {path, lowercased HTTP method}
    static const std::vector<std::pair<std::string, std::string>> kEndpoints = {
      {"/oauth2/token", "post"},
      {"/oauth2/authorize", "get"},
      {"/oauth2/userinfo", "get"},
      {"/oauth2/introspect", "post"},
      {"/oauth2/revoke", "post"},
      {"/.well-known/openid-configuration", "get"},
      {"/.well-known/jwks.json", "get"},
    };
    return kEndpoints;
}
}  // namespace

// 1.1 BASELINE: the full OpenApi endpoint set is registered and present.
// A missing endpoint here is the deterministic stand-in for a SIOF
// counterexample (global ctor order leaving the registry incomplete).
DROGON_TEST(Unit_InitOrder_1_1_OpenApiEndpointSet_Complete_Baseline)
{
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();

    REQUIRE(spec.isMember("paths"));
    const Json::Value &paths = spec["paths"];

    for (const auto &[path, method] : expectedEndpoints())
    {
        // Endpoint path must be present — a missing key is the SIOF symptom.
        CHECK(paths.isMember(path));
        if (paths.isMember(path))
        {
            CHECK(paths[path].isMember(method));
        }
    }
}

// 1.1 BASELINE: deep snapshot of representative endpoints — params, response
// examples, security, tags — must be complete. This is the byte-comparable
// baseline that task 6.4 preservation checks diff against after the SIOF fix.
DROGON_TEST(Unit_InitOrder_1_1_OpenApiTokenEndpoint_Snapshot_Baseline)
{
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    REQUIRE(spec.isMember("paths"));
    REQUIRE(spec["paths"].isMember("/oauth2/token"));

    const Json::Value &token = spec["paths"]["/oauth2/token"]["post"];

    // Summary / description / tags present and non-empty.
    CHECK(token["summary"].asString() == "Exchange authorization code for access token");
    CHECK(!token["description"].asString().empty());
    REQUIRE(token.isMember("tags"));
    CHECK(token["tags"].size() == 2);

    // The token endpoint registers exactly 6 query parameters.
    REQUIRE(token.isMember("parameters"));
    CHECK(token["parameters"].size() == 6);

    // Collect parameter names to assert the complete set (order-independent).
    std::set<std::string> paramNames;
    for (const auto &p : token["parameters"])
    {
        paramNames.insert(p["name"].asString());
        CHECK(p["in"].asString() == "query");
    }
    const std::set<std::string> expectedParams =
      {"grant_type", "code", "refresh_token", "client_id", "client_secret", "redirect_uri"};
    CHECK(paramNames == expectedParams);

    // grant_type advertises its enum values (completeness of param metadata).
    for (const auto &p : token["parameters"])
    {
        if (p["name"].asString() == "grant_type")
        {
            REQUIRE(p["schema"].isMember("enum"));
            CHECK(p["schema"]["enum"].size() == 3);
        }
    }

    // Response examples baseline: 200 has a JSON example with access_token.
    REQUIRE(token.isMember("responses"));
    CHECK(token["responses"].isMember("200"));
    CHECK(token["responses"].isMember("400"));
    CHECK(token["responses"].isMember("401"));
    CHECK(
      token["responses"]["200"]["content"]["application/json"]["example"].isMember("access_token")
    );
}

// 1.1 BASELINE: discovery + JWKS endpoints registered (these are the endpoints
// explicitly called out by the task: /.well-known/openid-configuration, JWKS).
DROGON_TEST(Unit_InitOrder_1_1_OpenApiDiscoveryAndJwks_Snapshot_Baseline)
{
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    REQUIRE(spec.isMember("paths"));
    const Json::Value &paths = spec["paths"];

    REQUIRE(paths.isMember("/.well-known/openid-configuration"));
    const Json::Value &disc = paths["/.well-known/openid-configuration"]["get"];
    CHECK(disc["summary"].asString() == "OpenID Connect Discovery");
    CHECK(disc["responses"].isMember("200"));
    // Public endpoint: must NOT carry a bearerAuth security requirement.
    CHECK(!disc.isMember("security"));

    REQUIRE(paths.isMember("/.well-known/jwks.json"));
    const Json::Value &jwks = paths["/.well-known/jwks.json"]["get"];
    CHECK(jwks["summary"].asString() == "JSON Web Key Set");
    CHECK(jwks["responses"].isMember("200"));
    CHECK(!jwks.isMember("security"));

    // Authenticated endpoints must carry bearerAuth security (completeness of
    // the security wiring that SIOF could otherwise drop).
    REQUIRE(paths.isMember("/oauth2/userinfo"));
    CHECK(paths["/oauth2/userinfo"]["get"].isMember("security"));
    REQUIRE(paths.isMember("/oauth2/introspect"));
    CHECK(paths["/oauth2/introspect"]["post"].isMember("security"));
    REQUIRE(paths.isMember("/oauth2/revoke"));
    CHECK(paths["/oauth2/revoke"]["post"].isMember("security"));
}

// ---------------------------------------------------------------------------
// 1.2 BASELINE: RequestValidationFilter rule completeness per path.
//
// OAUTH2_VALIDATION_RULES and getValidationRules() are PRIVATE, so we exercise
// the rule set through the public doFilter() interface. doFilter() looks up
// rules by req->path(): when a path has an ENABLED, NON-EMPTY rule set, a
// request that violates a rule must be REJECTED (FilterCallback fires with an
// error response); a request that satisfies all rules must PASS (the
// FilterChainCallback fires). The SIOF counterexample for 1.2 would be an
// EMPTY rule map (global read before the call_once fill), which would cause a
// configured path to silently PASS everything. These baselines lock in the
// current correct accept/reject behavior.
// ---------------------------------------------------------------------------
namespace
{
enum class FilterOutcome
{
    Passed,    // FilterChainCallback invoked (validation passed)
    Rejected,  // FilterCallback invoked with an error response
    Neither
};

// Runs RequestValidationFilter::doFilter synchronously (validation is sync)
// and reports which terminal callback fired.
FilterOutcome runValidation(const ::drogon::HttpRequestPtr &req)
{
    RequestValidationFilter filter;
    FilterOutcome outcome = FilterOutcome::Neither;

    auto fcb = [&outcome](const ::drogon::HttpResponsePtr &) { outcome = FilterOutcome::Rejected; };
    auto fccb = [&outcome]() { outcome = FilterOutcome::Passed; };

    filter.doFilter(req, std::move(fcb), std::move(fccb));
    return outcome;
}
}  // namespace

// 1.2 BASELINE: every configured path has a NON-EMPTY, ENABLED rule set.
// Proven by: a valid request PASSES, and a rule-violating request is REJECTED.
// If the global rule map were empty (the SIOF symptom), the violating request
// would wrongly PASS — so the "Rejected" assertions are the counterexample
// detectors.
DROGON_TEST(Unit_InitOrder_1_2_Authorize_RulesComplete_Baseline)
{
    // Valid authorize request -> passes.
    auto ok = ::drogon::HttpRequest::newHttpRequest();
    ok->setMethod(::drogon::Get);
    ok->setPath("/oauth2/authorize");
    ok->setParameter("client_id", "test-client");
    ok->setParameter("redirect_uri", "http://localhost:5173/callback");
    ok->setParameter("response_type", "code");
    CHECK(runValidation(ok) == FilterOutcome::Passed);

    // Missing required client_id -> rejected (proves rule is present & enabled).
    auto bad = ::drogon::HttpRequest::newHttpRequest();
    bad->setMethod(::drogon::Get);
    bad->setPath("/oauth2/authorize");
    bad->setParameter("redirect_uri", "http://localhost:5173/callback");
    bad->setParameter("response_type", "code");
    CHECK(runValidation(bad) == FilterOutcome::Rejected);
}

DROGON_TEST(Unit_InitOrder_1_2_Token_RulesComplete_Baseline)
{
    // Valid token request -> passes.
    auto ok = ::drogon::HttpRequest::newHttpRequest();
    ok->setMethod(::drogon::Post);
    ok->setPath("/oauth2/token");
    ok->setParameter("grant_type", "authorization_code");
    CHECK(runValidation(ok) == FilterOutcome::Passed);

    // Missing required grant_type -> rejected.
    auto bad = ::drogon::HttpRequest::newHttpRequest();
    bad->setMethod(::drogon::Post);
    bad->setPath("/oauth2/token");
    CHECK(runValidation(bad) == FilterOutcome::Rejected);
}

DROGON_TEST(Unit_InitOrder_1_2_Login_RulesComplete_Baseline)
{
    // Valid login (body fields fall back to request parameters) -> passes.
    auto ok = ::drogon::HttpRequest::newHttpRequest();
    ok->setMethod(::drogon::Post);
    ok->setPath("/oauth2/login");
    ok->setParameter("username", "alice");
    ok->setParameter("password", "password123");  // >= 8 chars, valid pattern
    CHECK(runValidation(ok) == FilterOutcome::Passed);

    // Password too short (< 8) -> rejected (proves min-length rule is present).
    auto bad = ::drogon::HttpRequest::newHttpRequest();
    bad->setMethod(::drogon::Post);
    bad->setPath("/oauth2/login");
    bad->setParameter("username", "alice");
    bad->setParameter("password", "short");
    CHECK(runValidation(bad) == FilterOutcome::Rejected);
}

DROGON_TEST(Unit_InitOrder_1_2_Register_RulesComplete_Baseline)
{
    // Valid register -> passes.
    auto ok = ::drogon::HttpRequest::newHttpRequest();
    ok->setMethod(::drogon::Post);
    ok->setPath("/api/register");
    ok->setParameter("username", "bob");
    ok->setParameter("password", "password123");
    CHECK(runValidation(ok) == FilterOutcome::Passed);

    // Missing required password -> rejected.
    auto bad = ::drogon::HttpRequest::newHttpRequest();
    bad->setMethod(::drogon::Post);
    bad->setPath("/api/register");
    bad->setParameter("username", "bob");
    CHECK(runValidation(bad) == FilterOutcome::Rejected);
}

// 1.2 BASELINE (negative control): a path with NO configured rules has an
// empty/disabled rule set and must PASS through unconditionally. This pins the
// "no rules => pass" branch so a future regression that accidentally attaches
// rules to unconfigured paths is caught.
DROGON_TEST(Unit_InitOrder_1_2_UnconfiguredPath_PassesThrough_Baseline)
{
    auto req = ::drogon::HttpRequest::newHttpRequest();
    req->setMethod(::drogon::Get);
    req->setPath("/health");
    CHECK(runValidation(req) == FilterOutcome::Passed);
}
