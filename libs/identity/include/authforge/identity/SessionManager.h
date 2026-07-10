#pragma once

// M2.5 identity completion, Session slice (authforge-sdk-refactor,
// design.md §4.1 rule 1/rule 2, §5.1/§6): real (non-placeholder)
// implementation. Extracts exactly the two identity/session-layer
// pieces that did not exist anywhere yet, out of
// libs/drogon/src/controllers/SessionController.cc's login()/logout()
// handlers:
//
//   1. evaluateLoginPolicy() -- the pure decision logic behind
//      login()'s "=== CHECK 1: Email verification enforcement ===" and
//      "=== CHECK 2: MFA enforcement ===" inline if/else chain. Pure,
//      synchronous, framework-independent -- no DB, no async callback
//      (there is no I/O in the decision itself; only the actual token
//      issuance and MFA-pending-binding *persistence* -- both handled
//      elsewhere, by oauth2::TokenService and
//      MfaService::setPendingBinding respectively -- are async, and
//      both are out of scope for this class).
//   2. logout() -- a thin one-method forward to an injected
//      IBackchannelLogoutNotifier, replacing SessionController.cc's
//      `sendBackchannelLogoutNotifications` stub.
//
// Scope boundary (read this before extending this class):
//   - Does NOT issue or revoke OAuth2 tokens. That's
//     oauth2::TokenService / OAuth2Plugin::generateAuthorizationCode /
//     OAuth2Plugin::revokeAccessToken (design.md §4.1 rule 2: identity
//     must never depend on oauth2).
//   - Does NOT manage MFA state (secrets, backup codes, pending
//     binding persistence). That's MfaService/IMfaRepository --
//     evaluateLoginPolicy only *signals* "MFA required"; it never reads
//     or writes any MFA data itself.
//   - Does NOT persist anything about user credentials, lockout, or
//     email-verification state. That's AuthService/IUserRepository --
//     evaluateLoginPolicy only *consumes* the already-computed
//     AuthResult and a config flag.
//   - Does NOT deliver back-channel logout notifications over HTTP.
//     That's a future Adapter-layer implementation of
//     IBackchannelLogoutNotifier (out of scope here, see that header's
//     top comment).
//
// This class owns exactly two things: the login policy decision, and
// backchannel logout notification forwarding.

#include <authforge/identity/AuthService.h>
#include <authforge/identity/IBackchannelLogoutNotifier.h>

#include <functional>
#include <memory>
#include <string>

namespace authforge::identity
{

/**
 * @brief Outcome of SessionManager::evaluateLoginPolicy.
 */
enum class LoginDecision
{
    Proceed,                 // Neither check blocked login -- caller should
                              // proceed to issue tokens (e.g.
                              // generateAuthorizationCode).
    DenyEmailNotVerified,    // Email verification is required and the
                              // user's email is not verified -- caller
                              // should deny the login (no tokens issued).
    RequireMfa,               // The user has MFA enabled -- caller should
                              // require MFA verification before issuing
                              // tokens (and persist the pending binding via
                              // MfaService::setPendingBinding, not this
                              // class).
};

/**
 * @brief Identity/session-layer service. See this header's top comment
 * for the exact scope boundary.
 */
class SessionManager
{
  public:
    /**
     * @brief Construct with the backchannel logout notification port.
     * @param notifier Backchannel logout notifier (required).
     */
    explicit SessionManager(std::shared_ptr<IBackchannelLogoutNotifier> notifier);

    /**
     * @brief Decide what should happen next after a successful
     * credential check, replacing SessionController::login()'s inline
     * "CHECK 1" / "CHECK 2" if/else chain. Preserves that code's exact
     * precedence: email-verification is checked *before* MFA, so an
     * unverified email is denied even if MFA is also enabled.
     *
     * Pure and synchronous -- no I/O, no side effects, safe to call
     * without an instance (see the free-function overload below) or
     * from any thread.
     *
     * @param authResult The already-computed successful authentication
     * result (AuthService::validateUser's callback argument).
     * @param requireEmailVerification Configuration flag (mirrors
     * SessionController.cc's `customCfg["auth"]
     * ["require_email_verification"]` read).
     * @return Proceed, DenyEmailNotVerified, or RequireMfa.
     */
    LoginDecision evaluateLoginPolicy(const AuthResult &authResult, bool requireEmailVerification)
      const;

    /**
     * @brief Forward a logout event to the injected
     * IBackchannelLogoutNotifier. Thin one-method forward -- does not
     * revoke tokens itself (see this header's top comment).
     * @param userId Identifier of the user whose session ended (passed
     * through opaquely, see IBackchannelLogoutNotifier::notify).
     * @param callback Invoked after the notifier completes.
     */
    void logout(const std::string &userId, std::function<void()> &&callback);

  private:
    std::shared_ptr<IBackchannelLogoutNotifier> notifier_;
};

/**
 * @brief Free-function form of SessionManager::evaluateLoginPolicy, for
 * callers that do not need/want to construct a SessionManager instance
 * just to make this stateless decision (e.g. unit tests, or a future
 * caller that only needs the policy decision and not backchannel
 * logout). Identical semantics to the member function.
 */
LoginDecision evaluateLoginPolicy(const AuthResult &authResult, bool requireEmailVerification);

}  // namespace authforge::identity
