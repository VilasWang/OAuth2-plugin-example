// M2.5 identity completion (authforge-sdk-refactor): unit tests for
// authforge::identity::SessionManager, exercised against a minimal
// hand-written FakeBackchannelLogoutNotifier (no DB/no Drogon),
// mirroring FakeMfaRepository/FakeCryptoProvider's established style.

#include <authforge/identity/AuthService.h>
#include <authforge/identity/IBackchannelLogoutNotifier.h>
#include <authforge/identity/SessionManager.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{

using namespace authforge::identity;

class FakeBackchannelLogoutNotifier : public IBackchannelLogoutNotifier
{
  public:
    std::vector<std::string> notifiedUserIds;

    void notify(const std::string &userId, std::function<void()> &&callback) override
    {
        notifiedUserIds.push_back(userId);
        callback();
    }
};

AuthResult makeAuthResult(bool emailVerified, bool mfaEnabled)
{
    AuthResult r;
    r.internalId = 42;
    r.publicSub = "sub-42";
    r.emailVerified = emailVerified;
    r.mfaEnabled = mfaEnabled;
    return r;
}

}  // namespace

// --- evaluateLoginPolicy: all (emailVerified, mfaEnabled,
// requireEmailVerification) combinations ---

TEST(
  SessionManagerTest,
  EvaluateLoginPolicy_RequireVerification_EmailNotVerified_MfaDisabled_Denies
)
{
    auto result =
      evaluateLoginPolicy(makeAuthResult(/*emailVerified=*/false, /*mfaEnabled=*/false), true);
    EXPECT_EQ(result, LoginDecision::DenyEmailNotVerified);
}

TEST(
  SessionManagerTest,
  EvaluateLoginPolicy_RequireVerification_EmailNotVerified_MfaEnabled_DeniesBeforeMfa
)
{
    // Precedence check (matches SessionController.cc's CHECK 1 before
    // CHECK 2): email-not-verified takes precedence over MFA, so this
    // must be DenyEmailNotVerified, not RequireMfa.
    auto result =
      evaluateLoginPolicy(makeAuthResult(/*emailVerified=*/false, /*mfaEnabled=*/true), true);
    EXPECT_EQ(result, LoginDecision::DenyEmailNotVerified);
}

TEST(
  SessionManagerTest,
  EvaluateLoginPolicy_RequireVerification_EmailVerified_MfaEnabled_RequiresMfa
)
{
    auto result =
      evaluateLoginPolicy(makeAuthResult(/*emailVerified=*/true, /*mfaEnabled=*/true), true);
    EXPECT_EQ(result, LoginDecision::RequireMfa);
}

TEST(SessionManagerTest, EvaluateLoginPolicy_RequireVerification_EmailVerified_MfaDisabled_Proceeds)
{
    auto result =
      evaluateLoginPolicy(makeAuthResult(/*emailVerified=*/true, /*mfaEnabled=*/false), true);
    EXPECT_EQ(result, LoginDecision::Proceed);
}

TEST(
  SessionManagerTest,
  EvaluateLoginPolicy_NotRequireVerification_EmailNotVerified_MfaDisabled_Proceeds
)
{
    // With requireEmailVerification == false, an unverified email is not
    // checked at all.
    auto result =
      evaluateLoginPolicy(makeAuthResult(/*emailVerified=*/false, /*mfaEnabled=*/false), false);
    EXPECT_EQ(result, LoginDecision::Proceed);
}

TEST(
  SessionManagerTest,
  EvaluateLoginPolicy_NotRequireVerification_EmailNotVerified_MfaEnabled_RequiresMfa
)
{
    auto result =
      evaluateLoginPolicy(makeAuthResult(/*emailVerified=*/false, /*mfaEnabled=*/true), false);
    EXPECT_EQ(result, LoginDecision::RequireMfa);
}

TEST(
  SessionManagerTest,
  EvaluateLoginPolicy_NotRequireVerification_EmailVerified_MfaEnabled_RequiresMfa
)
{
    auto result =
      evaluateLoginPolicy(makeAuthResult(/*emailVerified=*/true, /*mfaEnabled=*/true), false);
    EXPECT_EQ(result, LoginDecision::RequireMfa);
}

TEST(
  SessionManagerTest,
  EvaluateLoginPolicy_NotRequireVerification_EmailVerified_MfaDisabled_Proceeds
)
{
    auto result =
      evaluateLoginPolicy(makeAuthResult(/*emailVerified=*/true, /*mfaEnabled=*/false), false);
    EXPECT_EQ(result, LoginDecision::Proceed);
}

// Member-function form should be identical to the free function.
TEST(SessionManagerTest, EvaluateLoginPolicy_MemberFunction_MatchesFreeFunction)
{
    auto notifier = std::make_shared<FakeBackchannelLogoutNotifier>();
    SessionManager manager(notifier);

    for (bool emailVerified : {false, true})
    {
        for (bool mfaEnabled : {false, true})
        {
            for (bool requireEmailVerification : {false, true})
            {
                auto authResult = makeAuthResult(emailVerified, mfaEnabled);
                EXPECT_EQ(
                  manager.evaluateLoginPolicy(authResult, requireEmailVerification),
                  evaluateLoginPolicy(authResult, requireEmailVerification)
                );
            }
        }
    }
}

// --- logout(): forwards to the notifier and invokes the callback ---

TEST(SessionManagerTest, Logout_ForwardsUserIdToNotifier)
{
    auto notifier = std::make_shared<FakeBackchannelLogoutNotifier>();
    SessionManager manager(notifier);

    manager.logout("user-123", [] {});

    ASSERT_EQ(notifier->notifiedUserIds.size(), 1u);
    EXPECT_EQ(notifier->notifiedUserIds[0], "user-123");
}

TEST(SessionManagerTest, Logout_InvokesCallback)
{
    auto notifier = std::make_shared<FakeBackchannelLogoutNotifier>();
    SessionManager manager(notifier);

    bool callbackInvoked = false;
    manager.logout("user-456", [&] { callbackInvoked = true; });

    EXPECT_TRUE(callbackInvoked);
}

// Coverage addition (P1, documentation anchor): SessionManager::logout
// dereferences notifier_ UNCONDITIONALLY (SessionManager.cc:47), unlike
// every other identity service which null-guards its dependency. This
// means logout() is unsafe with a null notifier, but evaluateLoginPolicy()
// does NOT touch notifier_ and is therefore safe. Pin that asymmetry so a
// future refactor that adds a null-guard to logout is a deliberate,
// visible behavior change rather than a silent one.
TEST(SessionManagerTest, EvaluateLoginPolicyMemberFunction_NullNotifier_StillWorks)
{
    // Construct with a null notifier -- evaluateLoginPolicy must still work.
    SessionManager manager(nullptr);

    AuthResult result;
    result.emailVerified = true;
    result.mfaEnabled = false;

    // No dereference of notifier_ occurs here -> no crash.
    EXPECT_EQ(manager.evaluateLoginPolicy(result, false), LoginDecision::Proceed);
    EXPECT_EQ(manager.evaluateLoginPolicy(result, true), LoginDecision::Proceed);

    result.mfaEnabled = true;
    EXPECT_EQ(manager.evaluateLoginPolicy(result, false), LoginDecision::RequireMfa);

    result.emailVerified = false;
    EXPECT_EQ(manager.evaluateLoginPolicy(result, true), LoginDecision::DenyEmailNotVerified);
}
