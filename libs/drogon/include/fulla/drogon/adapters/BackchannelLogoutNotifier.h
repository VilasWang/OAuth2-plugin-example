#pragma once

// B1 (OIDC Back-Channel Logout 1.0): the real Adapter-layer implementation of
// fulla::identity::IBackchannelLogoutNotifier. On logout it finds every
// relying party (oauth2_clients row) that has an active session for the user
// (a non-revoked, non-expired oauth2_access_tokens row) AND a registered
// backchannel_logout_uri, then POSTs a signed logout_token to each
// (OIDC Back-Channel Logout 1.0 §2.3/§2.4). Replaces the
// LoggingBackchannelLogoutNotifier stub wired in IdentityAssembly.cc.
//
// Trigger paths (#55) -- every logout flow the product performs notifies:
//   1. POST /oauth2/logout            (SessionController::logout)   -- subject
//      from the bearer token (OAuth2Middleware's userId attribute).
//   2. GET/POST /oauth2/end_session   (SessionController::endSession, OIDC
//      RP-Initiated Logout) -- subject from the browser session's "sub"
//      (stored at login) or the id_token_hint's sub claim. Used by the user
//      portal frontend.
//   3. The admin frontend calls POST /oauth2/logout directly (its store's
//      logout()).
// Frontends that only revoke tokens (RFC 7009) do NOT notify -- revocation is
// not a logout event.
//
// Layering: Adapter (libs/drogon) -- it depends on Drogon's orm::Mapper +
// IOAuthHttpClient + JwkManager, none of which a Domain-layer package may
// touch. Mirrors DrogonOAuthHttpClient's placement.
//
// Async/lifetime: notify() is callback-based (the port contract). The two DB
// lookups are chained async Mapper queries (no JOIN per db-operations.md);
// the outbound POSTs are fire-and-forget (the logout HTTP response is NOT
// deferred until RPs respond). Inherits enable_shared_from_this so async
// continuations keep the object alive; POST-completion lambdas capture only
// shared state (the audit sink), never this.

#include <fulla/common/ports/IAuditSink.h>
#include <fulla/identity/IBackchannelLogoutNotifier.h>
#include <fulla/identity/IOAuthHttpClient.h>
#include <fulla/oauth2/jwk/JwkManager.h>
#include <fulla/oauth2/protocol/LogoutToken.h>

#include <drogon/orm/DbClient.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fulla::drogon::adapters
{

/// One relying party to notify: its client_id (becomes the logout_token
/// `aud`) and its registered backchannel_logout_uri (the POST destination).
struct BackchannelRpTarget
{
    std::string clientId;
    std::string backchannelLogoutUri;
};

class BackchannelLogoutNotifier
  : public fulla::identity::IBackchannelLogoutNotifier,
    public std::enable_shared_from_this<BackchannelLogoutNotifier>
{
  public:
    /// @param dbClient   Looks up active sessions + client URIs.
    /// @param jwkManager OP signing key (signs the logout_token). Production
    ///                   wiring publishes a shared_ptr<const> after init().
    /// @param issuer     OP issuer (logout_token `iss`).
    /// @param httpClient Outbound POST transport (POSTs logout_token as form).
    /// @param auditSink  Optional audit sink (nullptr -> no audit records).
    /// @param tokenTtlSeconds  logout_token exp-iat (short; spec <= 120s).
    BackchannelLogoutNotifier(
      ::drogon::orm::DbClientPtr dbClient,
      std::shared_ptr<const fulla::oauth2::JwkManager> jwkManager,
      std::string issuer,
      std::shared_ptr<fulla::identity::IOAuthHttpClient> httpClient,
      std::shared_ptr<fulla::common::ports::IAuditSink> auditSink = nullptr,
      int tokenTtlSeconds = fulla::oauth2::protocol::kLogoutTokenDefaultTtlSeconds);

    /// IBackchannelLogoutNotifier: fan a logout_token out to every affected RP.
    /// `callback` is invoked once dispatch is complete (fire-and-forget from
    /// the caller's perspective; NOT deferred to RP POST completion).
    void notify(const std::string &userId, std::function<void()> &&callback) override;

    /// Unit-testable fan-out over an already-resolved RP set: signs + POSTs a
    /// logout_token to each target's URI, auditing each attempt, then invokes
    /// `completion`. Does NOT touch the DB. Exposed (not private) so the
    /// dispatch logic can be tested without a database.
    void dispatch(
      const std::string &subject,
      const std::vector<BackchannelRpTarget> &targets,
      std::function<void()> &&completion);

  private:
    ::drogon::orm::DbClientPtr dbClient_;
    std::shared_ptr<const fulla::oauth2::JwkManager> jwkManager_;
    std::string issuer_;
    std::shared_ptr<fulla::identity::IOAuthHttpClient> httpClient_;
    std::shared_ptr<fulla::common::ports::IAuditSink> auditSink_;
    int tokenTtlSeconds_;
};

}  // namespace fulla::drogon::adapters
