#pragma once

// M2.5 identity completion, Session slice (authforge-sdk-refactor,
// design.md §4.1 rule 1/rule 2, §5.1/§6): outbound-notification port
// backing SessionManager::logout. Replaces
// libs/drogon/src/controllers/SessionController.cc's
// `static void sendBackchannelLogoutNotifications(const std::string &)`
// stub (currently just `LOG_DEBUG << "sendBackchannelLogoutNotifications:
// stub";`) with a real seam.
//
// Scope boundary: a real implementation of this port would need to look
// up each relying party's registered back-channel logout URI and POST a
// signed `logout_token` to it (OIDC Back-Channel Logout 1.0) -- that
// requires HTTP transport and, likely, client-registration lookups. Both
// are Adapter-layer/oauth2-domain concerns and are explicitly OUT OF
// SCOPE here, same as IOAuthHttpClient's real drogon::HttpClient-backed
// implementation was out of scope for the Social auth slice (see that
// header's own top comment). This interface only defines the one-method
// shape SessionManager depends on; the real implementation is deferred
// to a future Adapter-layer task.
//
// Async (not synchronous) to match the codebase's established port
// convention (IMfaRepository/IOAuthHttpClient/etc. are all callback-based)
// even though the current stub it replaces is synchronous -- a real
// implementation will need to make outbound HTTP calls.

#include <functional>
#include <string>

namespace authforge::identity
{

/**
 * @brief Outbound notification port for OIDC-style back-channel logout.
 *
 * SessionManager::logout() calls this port with the internal user id
 * whose session just ended; a real (Adapter-layer) implementation would
 * fan out to each affected relying party's registered back-channel
 * logout endpoint. See this header's top comment for the scope
 * boundary.
 */
class IBackchannelLogoutNotifier
{
  public:
    virtual ~IBackchannelLogoutNotifier() = default;

    /**
     * @brief Notify relying parties that the given user's session ended.
     * @param userId Internal user id (or public subject, up to the
     * caller's convention -- SessionManager passes through whatever
     * identifier it was given without interpreting it) whose session
     * ended.
     * @param callback Invoked once notification has been attempted
     * (fire-and-forget from the caller's perspective; this port does not
     * report per-relying-party success/failure).
     */
    virtual void notify(const std::string &userId, std::function<void()> &&callback) = 0;
};

}  // namespace authforge::identity
