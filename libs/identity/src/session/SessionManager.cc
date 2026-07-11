// M2.5 identity completion, Session slice (authforge-sdk-refactor):
// real (non-placeholder) implementation. See SessionManager.h's top
// comment for the exact scope boundary.

#include <authforge/identity/SessionManager.h>

#include <utility>

namespace authforge::identity
{

LoginDecision evaluateLoginPolicy(const AuthResult &authResult, bool requireEmailVerification)
{
    // === CHECK 1: Email verification enforcement ===
    // Mirrors SessionController.cc's login() precedence exactly: this
    // check runs before the MFA check, so an unverified email denies
    // login even when MFA is also enabled.
    if (requireEmailVerification && !authResult.emailVerified)
    {
        return LoginDecision::DenyEmailNotVerified;
    }

    // === CHECK 2: MFA enforcement ===
    if (authResult.mfaEnabled)
    {
        return LoginDecision::RequireMfa;
    }

    return LoginDecision::Proceed;
}

SessionManager::SessionManager(std::shared_ptr<IBackchannelLogoutNotifier> notifier)
    : notifier_(std::move(notifier))
{
}

LoginDecision SessionManager::evaluateLoginPolicy(
  const AuthResult &authResult,
  bool requireEmailVerification
) const
{
    return authforge::identity::evaluateLoginPolicy(authResult, requireEmailVerification);
}

void SessionManager::logout(const std::string &userId, std::function<void()> &&callback)
{
    notifier_->notify(userId, std::move(callback));
}

}  // namespace authforge::identity
