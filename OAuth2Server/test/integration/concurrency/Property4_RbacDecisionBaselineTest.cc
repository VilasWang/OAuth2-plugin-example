// OAuth2Server/test/integration/concurrency/Property4_RbacDecisionBaselineTest.cc
//
// Spec: concurrency-lifetime-safety-audit — Task 4 (Property 4: Preservation).
// Behavioral Equivalence on ¬C(X). Observation object **3.7**: after the
// AuthorizationFilter has finished its (one-time) initialization, its RBAC and
// public-path decision for authenticated / unauthenticated requests — i.e. the
// allow (pass) / 401 / 403 outcome — must be preserved across the category-B
// fix (7.1, which replaces the racy check-then-act `loadConfig()` with a
// call_once / startup-time load; re-verified at 7.4). The 7.1 fix only changes
// HOW the rules are loaded once (thread-safely); it must NOT change WHICH
// decision a loaded rule set produces for any request.
//
// ─────────────────────────────────────────────────────────────────────────
// WHY THIS IS A NEW (NON-DUPLICATE) BASELINE
// ─────────────────────────────────────────────────────────────────────────
// Task 1 (unit/initorder/CategoryA_InitOrderSnapshotTest.cc) froze the
// RequestValidationFilter rule set (3.1) — a DIFFERENT filter that checks
// request PARAMETERS. The Task-2 race test (CategoryB_AuthorizationFilterRace
// Test.cc) only proves the loadConfig() DATA RACE; it does not pin the RBAC
// DECISION semantics. None of the six existing Property4_* files capture 3.7.
// This file fills that gap: it pins the AuthorizationFilter's allow/401/403
// contract as the observation-first baseline that 7.4 re-runs on F'.
//
// ─────────────────────────────────────────────────────────────────────────
// WHAT IS PINNED, AND AT WHICH FAITHFULNESS LEVEL (with an honest caveat)
// ─────────────────────────────────────────────────────────────────────────
//   (1) THE REAL FILTER, UNAUTHENTICATED PATH (no model): driving the genuine
//       production `AuthorizationFilter::doFilter()` with a token-less request
//       runs the real (one-time) `loadConfig()` and then returns the real
//       `401 {"error":"unauthorized"}` BEFORE touching the OAuth2Plugin /
//       token validation / DB. This is the actual, externally observable
//       unauthenticated outcome and needs no external service. (It is also the
//       exact entry point the Task-2 race test drives, reused here for the
//       DECISION rather than the race.)
//
//   (2) THE RBAC DECISION BRANCH STRUCTURE (faithful local model): the
//       authenticated allow/403 decision lives in the PRIVATE
//       `checkAccess(roles, path)` and only runs AFTER a token is validated via
//       `plugin->validateAccessToken()` → `plugin->getUserRoles()`, both of
//       which require a live storage backend (the test config uses
//       `storage_type: postgres`). On this host (no DB; see Task 0) driving the
//       genuine private decision end-to-end is not possible, so we mirror
//       `checkAccess()` BYTE-FOR-BYTE in `RbacDecisionModel` — same
//       `std::regex_match` semantics, same "public-path-first → matched-rule
//       role check → matched-but-no-role DENY → default DENY" branch order —
//       and seed it with the SAME `rbac_rules` the test `config.json` ships
//       (`/api/admin/.* → [admin]`, `/api/user/.* → [user, admin]`). A faithful
//       end-to-end capture of the real authenticated 403/allow path requires a
//       **Postgres-backed CI build** — documented here rather than fabricated.
//
// The PBT cross-checks the production-mirroring regex model against an
// INDEPENDENT string-prefix decision table (not a tautology): for random normal
// (path, roles) inputs the two must agree, and that agreed decision is mapped to
// the allow/401/403 outcome exactly as `doFilter()` structures it.
//
// METHODOLOGY: observation-first, runs on UNFIXED code (F), MUST PASS. No
// production code modified.
//
// **Validates: Requirements 3.7**
//
// _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7_

#include <drogon/drogon_test.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include <regex>
#include <string>
#include <vector>

#include <oauth2/filters/AuthorizationFilter.h>

#include "Property4_PreservationSupport.h"

using namespace oauth2::test::concurrency;
using authforge::drogon::filters::AuthorizationFilter;

namespace
{
// The three terminal outcomes the AuthorizationFilter can produce, matching the
// task's "allow / 401 / 403" contract.
enum class RbacOutcome
{
    Allow,            // FilterChainCallback fired (request passes / allow)
    Unauthorized401,  // 401 {"error":"unauthorized" | "invalid_token"}
    Forbidden403      // 403 {"error":"forbidden"}
};

// ─────────────────────────────────────────────────────────────────────────
// RbacDecisionModel — a BYTE-FOR-BYTE mirror of the production private
// AuthorizationFilter::checkAccess(userRoles, path). It uses the SAME
// std::regex / std::regex_match calls and the SAME branch order so the decision
// it produces is the genuine production decision for a given rule set.
// ─────────────────────────────────────────────────────────────────────────
class RbacDecisionModel
{
  public:
    void addRule(const std::string &pattern, std::vector<std::string> allowedRoles)
    {
        Rule r;
        r.pathPattern = std::regex(pattern);
        r.allowedRoles = std::move(allowedRoles);
        rules_.push_back(std::move(r));
    }

    void addPublicPath(const std::string &pattern)
    {
        publicPaths_.push_back(std::regex(pattern));
    }

    // Mirrors AuthorizationFilter::checkAccess() branch-for-branch.
    bool checkAccess(const std::vector<std::string> &userRoles, const std::string &path) const
    {
        // Public paths first (no role required).
        for (const auto &publicPath : publicPaths_)
        {
            if (std::regex_match(path, publicPath))
                return true;
        }
        // RBAC rules.
        for (const auto &rule : rules_)
        {
            if (std::regex_match(path, rule.pathPattern))
            {
                for (const auto &allowed : rule.allowedRoles)
                {
                    for (const auto &userRole : userRoles)
                    {
                        if (userRole == allowed)
                            return true;
                    }
                }
                // Matched rule but roles did not -> DENY.
                return false;
            }
        }
        // DEFAULT DENY.
        return false;
    }

  private:
    struct Rule
    {
        std::regex pathPattern;
        std::vector<std::string> allowedRoles;
    };

    std::vector<Rule> rules_;
    std::vector<std::regex> publicPaths_;
};

// Mirrors the overall structure of doFilter(): no/invalid token -> 401; else the
// checkAccess() result maps to Allow / 403.
RbacOutcome decide(
  const RbacDecisionModel &model,
  bool hasValidToken,
  const std::vector<std::string> &roles,
  const std::string &path
)
{
    if (!hasValidToken)
        return RbacOutcome::Unauthorized401;
    return model.checkAccess(roles, path) ? RbacOutcome::Allow : RbacOutcome::Forbidden403;
}

// The rule set the test config.json ships under custom_config.rbac_rules.
// (No public_paths key is configured there, so publicPaths_ is empty — the
// deployed contract this test pins.)
RbacDecisionModel makeConfigModel()
{
    RbacDecisionModel m;
    m.addRule("/api/admin/.*", {"admin"});
    m.addRule("/api/user/.*", {"user", "admin"});
    return m;
}

// INDEPENDENT (non-regex) decision oracle used by the PBT to cross-check the
// production-mirroring regex model. Derives the expected allow decision from
// plain string prefixes over the config rule set — so the PBT compares two
// independent implementations rather than the model against itself.
bool independentExpectedAllow(const std::string &path, const std::vector<std::string> &roles)
{
    auto hasRole = [&roles](const std::string &want) {
        for (const auto &r : roles)
            if (r == want)
                return true;
        return false;
    };
    // "/api/admin/.*" : prefix "/api/admin/" with at least one trailing char.
    if (path.rfind("/api/admin/", 0) == 0 && path.size() > std::string("/api/admin/").size())
        return hasRole("admin");
    // "/api/user/.*" : prefix "/api/user/" with at least one trailing char.
    if (path.rfind("/api/user/", 0) == 0 && path.size() > std::string("/api/user/").size())
        return hasRole("user") || hasRole("admin");
    // No rule matched -> default deny.
    return false;
}

// Drive the REAL AuthorizationFilter with a token-less request and capture the
// terminal outcome. A token-less request is denied (401) inside doFilter BEFORE
// the OAuth2Plugin / token validation is reached, so this needs no DB.
struct RealFilterResult
{
    RbacOutcome outcome{RbacOutcome::Allow};
    int statusCode{0};
    std::string errorField;
};

RealFilterResult driveRealFilterNoToken(const std::string &path)
{
    AuthorizationFilter filter;
    RealFilterResult result;

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(path);
    // No Authorization header and no access_token parameter.

    auto deny = [&result](const drogon::HttpResponsePtr &resp) {
        result.statusCode = static_cast<int>(resp->getStatusCode());
        if (result.statusCode == static_cast<int>(drogon::k401Unauthorized))
            result.outcome = RbacOutcome::Unauthorized401;
        else if (result.statusCode == static_cast<int>(drogon::k403Forbidden))
            result.outcome = RbacOutcome::Forbidden403;

        // Parse JSON manually from body - Error Envelope format: {"error": {"code": "..."}}
        auto body = std::string(resp->getBody());
        if (!body.empty())
        {
            Json::Value json;
            Json::Reader reader;
            if (
              reader.parse(body, json) && json.isMember("error") && json["error"].isMember("code")
            )
            {
                result.errorField = json["error"]["code"].asString();
            }
        }
    };
    auto allow = [&result]() { result.outcome = RbacOutcome::Allow; };

    filter.doFilter(req, std::move(deny), std::move(allow));
    return result;
}
}  // namespace

// 3.7 PRESERVATION (REAL filter, unauthenticated -> 401): the genuine
// production doFilter() denies a token-less request with 401
// {"error":"unauthorized"} for any path, before any RBAC/role evaluation.
DROGON_TEST(Property4_3_7_RealFilter_NoToken_Returns401_Baseline)
{
    for (const std::string &path :
         {"/api/admin/users", "/api/user/profile", "/random/unmatched/path", "/api/public/health"})
    {
        auto r = driveRealFilterNoToken(path);
        CHECK(r.outcome == RbacOutcome::Unauthorized401);
        CHECK(r.statusCode == static_cast<int>(drogon::k401Unauthorized));
        CHECK(r.errorField == "AUTH_TOKEN_INVALID");
    }
}

// 3.7 PRESERVATION (admin rule): "/api/admin/.*" requires the "admin" role.
// admin role -> allow; non-admin role -> 403; no roles -> 403.
DROGON_TEST(Property4_3_7_AdminRule_RoleGated_Baseline)
{
    auto m = makeConfigModel();

    CHECK(decide(m, true, {"admin"}, "/api/admin/users") == RbacOutcome::Allow);
    CHECK(decide(m, true, {"admin", "user"}, "/api/admin/settings/reset") == RbacOutcome::Allow);
    CHECK(decide(m, true, {"user"}, "/api/admin/users") == RbacOutcome::Forbidden403);
    CHECK(decide(m, true, {}, "/api/admin/users") == RbacOutcome::Forbidden403);
    CHECK(decide(m, true, {"guest"}, "/api/admin/users") == RbacOutcome::Forbidden403);
}

// 3.7 PRESERVATION (user rule): "/api/user/.*" allows "user" OR "admin".
DROGON_TEST(Property4_3_7_UserRule_AllowsUserOrAdmin_Baseline)
{
    auto m = makeConfigModel();

    CHECK(decide(m, true, {"user"}, "/api/user/profile") == RbacOutcome::Allow);
    CHECK(decide(m, true, {"admin"}, "/api/user/orders/42") == RbacOutcome::Allow);
    CHECK(decide(m, true, {"user", "guest"}, "/api/user/profile") == RbacOutcome::Allow);
    CHECK(decide(m, true, {"guest"}, "/api/user/profile") == RbacOutcome::Forbidden403);
    CHECK(decide(m, true, {}, "/api/user/profile") == RbacOutcome::Forbidden403);
}

// 3.7 PRESERVATION (default deny): a path matching NO rule and NO public path is
// denied (403) regardless of how privileged the caller is.
DROGON_TEST(Property4_3_7_UnmatchedPath_DefaultDeny_Baseline)
{
    auto m = makeConfigModel();

    CHECK(decide(m, true, {"admin"}, "/random/unmatched/path") == RbacOutcome::Forbidden403);
    CHECK(decide(m, true, {"admin", "user"}, "/oauth2/token") == RbacOutcome::Forbidden403);
    CHECK(decide(m, true, {"admin"}, "/api/public/health") == RbacOutcome::Forbidden403);
    // boundary: "/api/admin" without a trailing segment does NOT match
    // "/api/admin/.*" -> default deny.
    CHECK(decide(m, true, {"admin"}, "/api/admin") == RbacOutcome::Forbidden403);
}

// 3.7 PRESERVATION (public-path branch): a configured public path is allowed for
// ANY caller (even with no roles), and it short-circuits BEFORE the RBAC rules —
// pinning the "public paths first" branch order even though the shipped test
// config configures none.
DROGON_TEST(Property4_3_7_PublicPath_AllowsAnyone_Baseline)
{
    RbacDecisionModel m = makeConfigModel();
    m.addPublicPath("/public/.*");
    // A public pattern that also overlaps an admin path proves "public first".
    m.addPublicPath("/api/admin/health");

    CHECK(decide(m, true, {}, "/public/docs") == RbacOutcome::Allow);
    CHECK(decide(m, true, {"guest"}, "/public/anything/here") == RbacOutcome::Allow);
    // Overlap: public-path match wins over the admin RBAC rule.
    CHECK(decide(m, true, {}, "/api/admin/health") == RbacOutcome::Allow);
    // A non-public admin path still requires the admin role.
    CHECK(decide(m, true, {}, "/api/admin/users") == RbacOutcome::Forbidden403);
}

// 3.7 PRESERVATION (PBT over random normal requests): for reproducible random
// (path, roles, token-present?) inputs, the production-mirroring regex model
// agrees with the INDEPENDENT string-prefix oracle, and the resulting
// allow/401/403 decision is stable. This is the byte-comparable decision
// baseline re-run on F' at 7.4.
DROGON_TEST(Property4_3_7_Rbac_RandomizedRequests_DecisionStable_Baseline)
{
    PreservationInputGen gen(0x3B7C3B7Cu);
    auto m = makeConfigModel();

    // Random role pool to draw subsets from.
    static const std::vector<std::string> kRolePool = {"admin", "user", "guest", "auditor"};

    constexpr int kRounds = 48;
    for (int round = 0; round < kRounds; ++round)
    {
        const std::string path = gen.requestPath();

        // Build a random role subset (0..3 roles).
        std::vector<std::string> roles;
        const int n = gen.intInRange(0, 3);
        for (int i = 0; i < n; ++i)
            roles.push_back(kRolePool[gen.pick(kRolePool.size())]);

        const bool hasValidToken = gen.boolean();

        // Cross-check: production-mirroring regex model == independent oracle.
        const bool modelAllow = m.checkAccess(roles, path);
        const bool oracleAllow = independentExpectedAllow(path, roles);
        CHECK(modelAllow == oracleAllow);

        // Outcome mapping mirrors doFilter(): no token -> 401; else allow/403.
        const RbacOutcome got = decide(m, hasValidToken, roles, path);
        RbacOutcome expected;
        if (!hasValidToken)
            expected = RbacOutcome::Unauthorized401;
        else
            expected = oracleAllow ? RbacOutcome::Allow : RbacOutcome::Forbidden403;
        CHECK(got == expected);
    }
}
