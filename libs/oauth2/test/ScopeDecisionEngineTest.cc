// Task 17 slice 7 (authforge-sdk-refactor): unit tests for the
// oauth2::access consent+scope decision engine.

#include <authforge/oauth2/access/ScopeDecisionEngine.h>

#include <gtest/gtest.h>

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

// ---------------------------------------------------------------------
// isAdminScope
// ---------------------------------------------------------------------

TEST(IsAdminScopeTest, ExactMatchesAreAdminScopes)
{
    EXPECT_TRUE(isAdminScope("admin"));
    EXPECT_TRUE(isAdminScope("admin:read"));
    EXPECT_TRUE(isAdminScope("admin:write"));
    EXPECT_TRUE(isAdminScope("user:manage"));
    EXPECT_TRUE(isAdminScope("settings:manage"));
}

TEST(IsAdminScopeTest, PrefixMatchIsAdminScope)
{
    // "admin:read:extra" matches via the "admin:read:" prefix rule (scope
    // starts with "admin:read" + ':'), reproducing
    // IdentityService::scopeRequiresAdminRole's exact semantics.
    EXPECT_TRUE(isAdminScope("admin:extra-detail"));
}

TEST(IsAdminScopeTest, NonAdminScopesReturnFalse)
{
    EXPECT_FALSE(isAdminScope("openid"));
    EXPECT_FALSE(isAdminScope("profile"));
    EXPECT_FALSE(isAdminScope("email"));
    EXPECT_FALSE(isAdminScope("administrator"));  // not a prefix match ("admin" + ':' required)
}

// ---------------------------------------------------------------------
// evaluateScope
// ---------------------------------------------------------------------

TEST(EvaluateScopeTest, NotInClientAllowlist_ReturnsInvalid)
{
    Client client = makeClient({"openid"});
    auto result = evaluateScope("profile", client, /*hasAdminRole=*/true, /*hasConsent=*/true);

    EXPECT_EQ(result.decision, ScopeDecision::Invalid);
    EXPECT_EQ(result.reason, "scope_not_allowed_for_client");
}

TEST(EvaluateScopeTest, AdminScopeWithoutAdminRole_ReturnsInvalid)
{
    Client client = makeClient({"admin"});
    auto result = evaluateScope("admin", client, /*hasAdminRole=*/false, /*hasConsent=*/true);

    EXPECT_EQ(result.decision, ScopeDecision::Invalid);
    EXPECT_EQ(result.reason, "admin_role_required");
}

TEST(EvaluateScopeTest, AdminScopeWithAdminRoleButNoConsent_ReturnsConsentRequired)
{
    Client client = makeClient({"admin"});
    auto result = evaluateScope("admin", client, /*hasAdminRole=*/true, /*hasConsent=*/false);

    EXPECT_EQ(result.decision, ScopeDecision::ConsentRequired);
    EXPECT_EQ(result.reason, "user_consent_required");
}

TEST(EvaluateScopeTest, NonAdminScopeWithoutConsent_ReturnsConsentRequired)
{
    Client client = makeClient({"openid"});
    auto result = evaluateScope("openid", client, /*hasAdminRole=*/false, /*hasConsent=*/false);

    EXPECT_EQ(result.decision, ScopeDecision::ConsentRequired);
}

TEST(EvaluateScopeTest, AllowedAndConsented_ReturnsValid)
{
    Client client = makeClient({"openid"});
    auto result = evaluateScope("openid", client, /*hasAdminRole=*/false, /*hasConsent=*/true);

    EXPECT_EQ(result.decision, ScopeDecision::Valid);
    EXPECT_TRUE(result.reason.empty());
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
      }
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
      });

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
      [](const std::string &scope) { return scope == "openid"; }  // only "openid" consented
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
      }
    );

    EXPECT_FALSE(consentCheckedForAdmin);
    ASSERT_EQ(summary.invalid.size(), 1u);
    EXPECT_EQ(summary.invalid[0], "admin");
}

TEST(EvaluateScopesTest, EmptyScopeList_CanProceedTrue)
{
    Client client = makeClient({});
    auto summary =
      evaluateScopes({}, client, /*hasAdminRole=*/false, [](const std::string &) { return true; });

    EXPECT_TRUE(summary.canProceed());
    EXPECT_TRUE(summary.valid.empty());
}

}  // namespace
