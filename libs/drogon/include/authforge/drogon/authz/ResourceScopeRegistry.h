#pragma once

// Resource-scope authorization model (#43).
//
// A single declarative registry of (path, method) -> required-scopes that is
// the authoritative source consulted by BOTH resource-access filters
// (OAuth2AuthFilter for user routes, AuthorizationFilter for admin routes)
// and the OpenAPI generator. Replaces the two per-filter hardcoded
// requiredScopeForPath / requiredAdminScopeForPath functions (F-010 minimal
// enforcement) with a queryable, fine-grained, implication-aware model.
//
// Population: built once at startup from OpenApiGenerator::endpoints() --
// each controller's initApiDocsImpl() declares requiredScopes/impliedBy on
// its EndpointInfo, then ResourceScopeRegistry::buildFromEndpoints() folds
// them into this registry. Immutable after startup (queries are lock-free
// reads over a fully-populated structure).

#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <drogon/HttpTypes.h>

#include <string>
#include <string_view>
#include <vector>

namespace authforge::drogon::authz
{

/// AND vs OR semantics for a multi-scope requirement.
enum class ScopeMatch
{
    All,  // token must carry (or imply) every listed scope
    Any   // token must carry (or imply) at least one listed scope
};

/// The scope requirement for one (path, method) route. A token satisfies the
/// requirement when, for each `scopes` entry, the token either carries that
/// exact scope OR carries a scope listed in `impliedBy` (implication is
/// per-requirement, not a global graph -- the `admin` super-scope is listed
/// in every admin-resource requirement's `impliedBy`).
struct ResourceScopeRequirement
{
    std::vector<std::string> scopes;
    ScopeMatch match = ScopeMatch::All;
    std::vector<std::string> impliedBy;
};

class ResourceScopeRegistry
{
  public:
    /// A registry entry as exposed for discovery (snapshot()).
    struct Entry
    {
        std::string path;  // template path, e.g. "/api/admin/users/{userId}"
        std::string method;  // "GET", "POST", ...
        ResourceScopeRequirement requirement;
    };

    /// Populate the registry from OpenApiGenerator::endpoints(). Idempotent
    /// (safe to call more than once; rebuilds from the current endpoint set).
    /// Call AFTER all controllers' initApiDocs() have run.
    static void buildFromEndpoints();

    /// Register a catch-all prefix requirement: ANY method on `prefix` or any
    /// subpath of `prefix` (with a '/' boundary) satisfies this requirement.
    /// Used for path families that the old filters gated by prefix (e.g.
    /// /api/me -> `profile` covers /api/me/mfa/*, /api/me/webauthn/*, ...).
    /// Exact-template entries (from buildFromEndpoints) take priority over
    /// prefix entries in lookup(). Call after buildFromEndpoints().
    static void registerPrefix(const std::string &prefix, const ResourceScopeRequirement &req);

    /// Look up the scope requirement for a concrete request path + HTTP
    /// method. Returns nullptr when the route has no scope requirement at
    /// this layer (public / unconfigured). Path parameters in the registered
    /// template (e.g. {userId}) match any single path segment. Falls back to
    /// prefix entries (registerPrefix) when no exact template matches.
    static const ResourceScopeRequirement *lookup(
      std::string_view path, ::drogon::HttpMethod method);

    /// Snapshot the full (path, method) -> scopes matrix for the discovery
    /// endpoint and OpenAPI generation. Order is registration order.
    static std::vector<Entry> snapshot();

    /// Startup consistency check (bidirectional):
    ///  (a) every registry entry corresponds to a real registered route --
    ///      orphan entries (dead config) LOG_FATAL;
    ///  (b) every route under a known auth-gated path family
    ///      (/api/admin/*, /api/me*, /oauth2/userinfo) has a registry entry
    ///      -- a missing entry is a silent gap, LOG_FATAL.
    /// Mirrors the OAUTH2_AUTO_MIGRATE loud-fail pattern.
    static void runConsistencyCheck();

    /// Test seam: clear the registry. Unit/integration tests that rebuild the
    /// registry with a custom endpoint set call this first.
    static void clear();

    /// Whether buildFromEndpoints() has populated the registry at least once.
    static bool isBuilt();

    /// Test seam: remove ONLY prefix entries (not exact-template entries from
    /// buildFromEndpoints). Used by unit tests that registerPrefix test-only
    /// prefixes and need to clean up without destroying the real registry.
    static void clearPrefixes();
};

}  // namespace authforge::drogon::authz
