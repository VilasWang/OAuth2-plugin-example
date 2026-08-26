// #71: server-side one-time state for the social LINK flow (login-CSRF /
// provider-code injection discipline, mirroring /oauth2/authorize's state).
//
// The SPA redirects to the provider carrying a state token minted here;
// the link-back POST must present that token, which is bound to
// (internal user id, provider) and single-use with a short TTL. Storage is
// an injected port: Redis in production (SET NX EX / GETDEL), an in-memory
// map in tests.
//
// Layer contract (same as ISocialAccountRepository): pure identity-layer
// interface, no framework types. Callback-capture discipline applies
// (db-operations.md): implementations copy the callback into async chains
// by value.

#pragma once

#ifdef WITH_SOCIAL

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace fulla::identity
{

/// What a consumed state token was bound to at issue time.
struct SocialLinkStateData
{
    int32_t internalUserId = 0;
    std::string provider;
};

class ISocialLinkStateStore
{
  public:
    virtual ~ISocialLinkStateStore() = default;

    /// Issue a fresh one-time state bound to (userId, provider).
    /// Callback receives the state token, or nullopt when the store is
    /// unavailable (fail-closed: linking must not proceed without it).
    using IssueCallback = std::function<void(std::optional<std::string>)>;
    virtual void issue(int32_t internalUserId, const std::string &provider, IssueCallback &&cb) = 0;

    /// Atomically consume a state token (single use). Callback receives the
    /// bound data, or nullopt when the token is unknown/expired/already used.
    using ConsumeCallback = std::function<void(std::optional<SocialLinkStateData>)>;
    virtual void consume(const std::string &state, ConsumeCallback &&cb) = 0;
};

}  // namespace fulla::identity

#endif  // WITH_SOCIAL
