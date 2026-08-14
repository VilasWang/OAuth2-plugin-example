// Task 17 slice 7 + #43 §5.5: unit tests for the oauth2::access consent+scope
// decision engine. The former hardcoded isAdminScope() tests are removed --
// the Tier-2 admin-role check is now driven by a caller-supplied predicate
// (backed by the DB oauth2_scopes.requires_admin_role column in production).

#include <authforge/oauth2/access/ScopeDecisionEngine.h>

#include <gtest/gtest.h>

#include <functional>

namespace
{

using namespace authforge::oauth2::access;
using authforge::oauth2::model::Client;
using authforge::oauth2::model::ClientType;
using authforge::oauth2::model::OAuth2Client;

Client makeClient(std::vector<std::string> allowedScopes)
{
    OAuth2Client dto;
    dto.clientId = "client-1";
    dto.clientType = ClientType::PUBLIC;
    dto.allowedScopes = std::move(allowedScopes);
    return Client(dto);
}

// #43 §5.5: the admin-scope predicate supplied to the engine. Mirrors what
// AuthorizationService builds from the DB-loaded set. "admin" is the only
// admin-tier scope exercised by these tests.
const std::function<bool(const std::string &)> kIsAdmin = [](const std::string &s) {
    return s == "admin";
};

// ---------------------------------------------------------------------
// evaluateScope
// ---------------------------------------------------------------------

TEST(EvaluateScopeTest, NotInClientAllowlist_ReturnsInvalid)
{
    Client client = makeClient({"openid"});
    auto result = evaluateScope("profile", client, /*hasAdminRole=*/true, /*hasConsent=*/true, kIsAdmin);

    EXPECT_EQ(result.decision, ScopeDecision::Invalid);
    EXPECT_EQ(result.reason, "scope_not_allowed_for_client");
}

TEST(EvaluateScopeTest, AdminScopeWithoutAdminRole_ReturnsInvalid)
{
    Client client = makeClient({"admin"});
    auto result = evaluateScope("admin", client, /*hasAdminRole=*/false, /*hasConsent=*/true, kIsAdmin);

    EXPECT_EQ(result.decision, ScopeDecision::Invalid);
    EXPECT_EQ(result.reason, "admin_role_required");
}

TEST(EvaluateScopeTest, AdminScopeWithAdminRoleButNoConsent_ReturnsConsentRequired)
{
    Client client = makeClient({"admin"});
    auto result = evaluateScope("admin", client, /*hasAdminRole=*/true, /*hasConsent=*/false, kIsAdmin);

    EXPECT_EQ(result.decision, ScopeDecision::ConsentRequired);
    EXPECT_EQ(result.reason, "user_consent_required");
}

TEST(EvaluateScopeTest, NonAdminScopeWithoutConsent_ReturnsConsentRequired)
{
    Client client = makeClient({"openid"});
    auto result = evaluateScope("openid", client, /*hasAdminRole=*/false, /*hasConsent=*/false, kIsAdmin);

    EXPECT_EQ(result.decision, ScopeDecision::ConsentRequired);
}

TEST(EvaluateScopeTest, AllowedAndConsented_ReturnsValid)
{
    Client client = makeClient({"openid"});
    auto result = evaluateScope("openid", client, /*hasAdminRole=*/false, /*hasConsent=*/true, kIsAdmin);

    EXPECT_EQ(result.decision, ScopeDecision::Valid);
    EXPECT_TRUE(result.reason.empty());
}

// evaluateScope: a non-admin ConsentRequired result carries the
// "user_consent_required" reason.
TEST(EvaluateScopeTest, NonAdminScopeWithoutConsent_ReasonIsUserConsentRequired)
{
    Client client = makeClient({"openid"});
    auto result = evaluateScope("openid", client, /*hasAdminRole=*/false, /*hasConsent=*/false, kIsAdmin);
    EXPECT_EQ(result.decision, ScopeDecision::ConsentRequired);
    EXPECT_EQ(result.reason, "user_consent_required");
}

// evaluateScope: the admin+role+consent combination returns Valid.
TEST(EvaluateScopeTest, AdminScopeWithAdminRoleAndConsent_ReturnsValid)
{
    Client client = makeClient({"admin"});
    auto result = evaluateScope("admin", client, /*hasAdminRole=*/true, /*hasConsent=*/true, kIsAdmin);
    EXPECT_EQ(result.decision, ScopeDecision::Valid);
    EXPECT_TRUE(result.reason.empty());
}

// evaluateScope: the result.scope field echoes the input scope verbatim.
TEST(EvaluateScopeTest, ResultScopeField_EchoesInput)
{
    Client client = makeClient({"openid"});
    auto result = evaluateScope("openid", client, false, true, kIsAdmin);
    EXPECT_EQ(result.scope, "openid");

    auto invalidResult = evaluateScope("nope", client, false, true, kIsAdmin);
    EXPECT_EQ(invalidResult.scope, "nope");
}

// ---------------------------------------------------------------------
// evaluateScopes
// ---------------------------------------------------------------------

TEST(EvaluateScopesTest, AllValid_CanProceedTrue)
{
    Client client = makeClient({"openid", "profile"});
    auto summary = evaluateScopes(
      {"openid", "profile"}, client, /*hasAdminRole=*/false, [](const std::string &) {
          return true;
      }, kIsAdmin
    );

    EXPECT_TRUE(summary.canProceed());
    EXPECT_FALSE(summary.needsConsent());
    EXPECT_FALSE(summary.hasErrors());
    EXPECT_EQ(summary.valid.size(), 2u);
}

TEST(EvaluateScopesTest, MixedValidAndInvalid_HasErrorsTrue)
{
    // "admin" is disallowed for this client entirely.
    Client client = makeClient({"openid"});
    auto summary =
      evaluateScopes({"openid", "admin"}, client, /*hasAdminRole=*/true, [](const std::string &) {
          return true;
      }, kIsAdmin);

    EXPECT_FALSE(summary.canProceed());
    EXPECT_TRUE(summary.hasErrors());
    ASSERT_EQ(summary.invalid.size(), 1u);
    EXPECT_EQ(summary.invalid[0], "admin");
    EXPECT_EQ(summary.invalidReasons[0], "scope_not_allowed_for_client");
    ASSERT_EQ(summary.valid.size(), 1u);
    EXPECT_EQ(summary.valid[0], "openid");
}

TEST(EvaluateScopesTest, NeedsConsent_WhenNoErrorsButUnconsentedScope)
{
    Client client = makeClient({"openid", "profile"});
    auto summary = evaluateScopes(
      {"openid", "profile"},
      client,
      /*hasAdminRole=*/false,
      [](const std::string &scope) { return scope == "openid"; },  // only "openid" consented
      kIsAdmin
    );

    EXPECT_FALSE(summary.canProceed());
    EXPECT_TRUE(summary.needsConsent());
    EXPECT_FALSE(summary.hasErrors());
    ASSERT_EQ(summary.valid.size(), 1u);
    EXPECT_EQ(summary.valid[0], "openid");
    ASSERT_EQ(summary.consentRequired.size(), 1u);
    EXPECT_EQ(summary.consentRequired[0], "profile");
}

TEST(EvaluateScopesTest, ConsentNeverCheckedForInvalidScope)
{
    // If a scope is Invalid on tier 1/2, hasConsentForScope must NOT be
    // invoked for it (matches production's short-circuit behavior).
    Client client = makeClient({"openid"});
    bool consentCheckedForAdmin = false;

    auto summary = evaluateScopes(
      {"admin"},
      client,
      /*hasAdminRole=*/true,
      [&](const std::string &scope) {
          if (scope == "admin")
              consentCheckedForAdmin = true;
          return true;
      },
      kIsAdmin
    );

    EXPECT_FALSE(consentCheckedForAdmin);
    ASSERT_EQ(summary.invalid.size(), 1u);
    EXPECT_EQ(summary.invalid[0], "admin");
}

TEST(EvaluateScopesTest, EmptyScopeList_CanProceedTrue)
{
    Client client = makeClient({});
    auto summary = evaluateScopes(
      {}, client, /*hasAdminRole=*/false, [](const std::string &) { return true; }, kIsAdmin);

    EXPECT_TRUE(summary.canProceed());
    EXPECT_TRUE(summary.valid.empty());
}

// evaluateScopes: multiple invalid scopes accumulate into invalidReasons in
// order.
TEST(EvaluateScopesTest, MultipleInvalidScopes_AccumulatesReasonsInOrder)
{
    Client client = makeClient({"openid"});  // admin + profile not allowed
    auto summary = evaluateScopes(
      {"profile", "admin", "openid"}, client, /*hasAdminRole=*/true,
      [](const std::string &) { return true; }, kIsAdmin
    );
    ASSERT_EQ(summary.invalid.size(), 2u);
    EXPECT_EQ(summary.invalid[0], "profile");
    EXPECT_EQ(summary.invalidReasons[0], "scope_not_allowed_for_client");
    EXPECT_EQ(summary.invalid[1], "admin");
    EXPECT_EQ(summary.invalidReasons[1], "scope_not_allowed_for_client");
    ASSERT_EQ(summary.valid.size(), 1u);
}

// evaluateScopes: multiple scopes all needing consent aggregate into
// consentRequired in order.
TEST(EvaluateScopesTest, AllScopesConsentRequired_NeedsConsentTrue)
{
    Client client = makeClient({"openid", "profile", "email"});
    auto summary = evaluateScopes(
      {"openid", "profile", "email"}, client, /*hasAdminRole=*/false,
      [](const std::string &) { return false; },  // nothing consented
      kIsAdmin
    );
    EXPECT_TRUE(summary.needsConsent());
    ASSERT_EQ(summary.consentRequired.size(), 3u);
    EXPECT_EQ(summary.consentRequired[0], "openid");
    EXPECT_EQ(summary.consentRequired[1], "profile");
    EXPECT_EQ(summary.consentRequired[2], "email");
}

// evaluateScopes: tier-2 short-circuit -- client ALLOWS "admin" but the
// user is not an admin -> Invalid without consulting hasConsentForScope.
TEST(EvaluateScopesTest, TierTwoReject_ClientAllowsAdminButNotAdminRole_SkipsConsent)
{
    Client client = makeClient({"admin"});  // client allows admin
    bool consentChecked = false;
    auto summary = evaluateScopes(
      {"admin"}, client, /*hasAdminRole=*/false, [&](const std::string &) {
          consentChecked = true;
          return true;
      }, kIsAdmin
    );
    EXPECT_FALSE(consentChecked);  // consent never consulted for an admin-role-rejected scope
    ASSERT_EQ(summary.invalid.size(), 1u);
    EXPECT_EQ(summary.invalidReasons[0], "admin_role_required");
}

// #43 §5.5: a scope NOT in the admin set does not trigger the admin-role
// check even if hasAdminRole is false -- the predicate is the sole authority
// on which scopes are admin-tiered.
TEST(EvaluateScopeTest, NonAdminScope_NotSubjectToAdminRoleCheck)
{
    Client client = makeClient({"users:read"});
    // kIsAdmin only matches "admin", not "users:read" -- so the engine does
    // NOT treat "users:read" as admin-tiered here (production would, but the
    // predicate is the test's to control). With consent, it is Valid.
    auto result = evaluateScope("users:read", client, /*hasAdminRole=*/false, /*hasConsent=*/true, kIsAdmin);
    EXPECT_EQ(result.decision, ScopeDecision::Valid);
}

}  // namespace
