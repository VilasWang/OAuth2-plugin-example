# Design: Full Resource-Scope Authorization Model (#43)

**Status:** Draft for review
**Author:** ZCode
**Created:** 2026-08-09
**Tracks:** GitHub issue #43 (successor to F-010 / #27 minimal scope enforcement)
**Related:** `oauth-oidc-compliance-audit.md` (F-010), `iam-architecture-audit.md`

---

## 1. Problem statement

The OAuth/OIDC audit's F-010 was implemented in #27 as a **minimal** path→required-scope
mapping enforced inside two filters:

- `OAuth2AuthFilter::requiredScopeForPath` — `/oauth2/userinfo` → `openid`; `/api/me` and `/api/me/*` → `profile`
- `AuthorizationFilter::requiredAdminScopeForPath` — `/api/admin` and `/api/admin/*` → `admin`

This closes the immediate RFC 6750 §3.1 `insufficient_scope` gap, but it is **not a real
authorization model**. The concrete shortcomings, all verified in the current code:

1. **Path-prefix matching, not per-endpoint.** Every `/api/admin/*` route (users CRUD, clients
   CRUD, token management, roles, scopes, dashboard, audit logs, OIDC keys) gets the single
   `admin` scope. There is no way to say "list users requires `admin:read` but delete users
   requires `admin:write`."
2. **No per-resource read/write granularity.** The scope catalog ships `read`/`write` rows
   (`V006__oauth2_scopes.sql:50-54`) but the resource-access filters never consult them.
3. **Three parallel registries, none authoritative.** The `ADD_METHOD_TO` filter-name string
   selects enforcement; two hardcoded `requiredScopeForPath` functions select the scope;
   `EndpointInfo` (OpenApiGenerator) documents auth but does not drive it. None reference the
   others. Adding a scope-gated route today means editing all three.
4. **Scope matching is exact-token, no implication.** `admin` does not imply `profile` or
   `openid` (`ScopeChecker.h:22-44`). A resource model that wants implication (an admin token
   satisfying `profile`-gated resources) must add that layer.
5. **The `admin` scope list is triple-hardcoded** (`IdentityService.cc:227-240`,
   `ScopeDecisionEngine.cc:6-24`, comments in `ScopeDecisionEngine.h:42`) and does **not**
   consult the DB column `oauth2_scopes.requires_admin_role` that exists for exactly this
   purpose — a known drift risk.
6. **Three independent `insufficient_scope` emitters** (`OAuth2AuthFilter.cc:58-64`,
   `AuthorizationFilter.cc:223-229`, `TokenEndpointController.cc:1885-1889`), one of which
   (the controller) omits the `scope="..."` WWW-Authenticate attribute and bypasses
   `ErrorResponder`.

This design defines the **complete** model: a single declarative registry of
`(path, method) → required-scopes`, finer-grained scopes, consistent downscoping, scope
implication, a single error path, and discovery of the scope→resource matrix.

---

## 2. Goals & non-goals

**Goals**
- One authoritative registry of `(path, method) → required-scopes`, consulted by enforcement,
  documented for OpenAPI, and consistency-checked.
- Per-resource read/write scope granularity (`<resource>:read`, `<resource>:write`) without
  a combinatorial explosion of scopes.
- Scope implication (a super-scope satisfies a sub-scope requirement) so admin tokens are not
  forced to carry every leaf scope.
- A single `insufficient_scope` error path (RFC 6750 §3.1) with consistent
  `WWW-Authenticate` + body.
- Remove the triple-hardcoded `admin` scope list in favor of the DB `requires_admin_role`
  column.
- Discovery: emit the scope→resource matrix (admin API + OpenAPI extension) so operators and
  SDK consumers can see what scope unlocks what.

**Non-goals**
- Attribute-based access control (ABAC) / policy engines (OPA, Cedar). This is
  scope-enhanced RBAC, not a general policy framework.
- Changing the token format or the `scope` claim representation (space-joined string stays).
- Replacing the existing RBAC role check. Scopes and roles remain complementary (a request
  must satisfy BOTH the scope gate AND the role gate for admin paths), matching today's
  `AuthorizationFilter` ordering.

---

## 3. RFC / standards basis

| Requirement | Basis |
|---|---|
| `insufficient_scope` error + 403 | RFC 6750 §3.1 (Bearer), §5.2 (error codes) |
| `scope` WWW-Authenticate attribute | RFC 6750 §3 (WWW-Authenticate `scope` auth-param) |
| Scope strings are space-delimited | RFC 6749 §3.3, RFC 6749 §5.4 |
| Scope is opaque to the authorization server's client | RFC 6749 §3.3 — the AS defines scope semantics |
| Per-operation `security` for OpenAPI | OpenAPI 3.0.3 §securityRequirement (already emitted correctly post-#41) |

OIDC Core §5.4 scopes-to-claims mapping (`profile`, `email`, `address`, `phone`) is
respected as-is for the `/oauth2/userinfo` path — this design extends, not replaces, those
standard mappings.

---

## 4. Current state (verified facts)

See the companion analysis (this section summarizes; full file:line evidence in the explore
report). Key facts the design builds on:

- **Scope catalog** (`V006__oauth2_scopes.sql`): `openid`, `profile`, `email`, `admin`,
  `read`, `write` are seeded; columns `mapped_role`, `is_default`, `requires_admin_role`
  exist but `requires_admin_role` is **not consulted at runtime** today.
- **Scope transport**: space-joined `TEXT` column on every grant/token table
  (`V002__oauth2_core.sql`); no normalized token↔scope table.
- **`EndpointInfo`** (`OpenApiGenerator.h:76-90`) already has `requiresAuth` + `authType`
  per endpoint, filled per-controller in `initApiDocsImpl()`. **This is the natural home for a
  `requiredScopes` field** — it is per-endpoint and filled at the source that already knows the
  path.
- **`hasAllScopes`** (`ScopeChecker.h:48-64`) already exists but is unused — the filter layer
  only checks a single scope today.
- **Filters** are Drogon `HttpController<T,false>`-attached by **string name** in
  `ADD_METHOD_TO`. The filter does not receive the route handler pointer; it sees only
  `req->path()` and `req->method()`.
- **`drogon::app().getHandlersInfo()`** exposes the full route table at runtime (path, method,
  filter names) — the project does not currently use it.

---

## 5. Proposed design

### 5.1 A single declarative registry: `ResourceScopeRegistry`

A new central registry maps `(path, method) → {requiredScopes, requireAny/All, implicationClass}`
and is the **single source of truth** consulted by both filters and the OpenAPI generator.

```cpp
// libs/drogon/include/authforge/drogon/authz/ResourceScopeRegistry.h
namespace authforge::drogon::authz {

enum class ScopeMatch { All, Any };  // ALL: token must hold every; ANY: at least one

struct ResourceScopeRequirement {
    std::vector<std::string> scopes;   // e.g. {"users:read"} or {"admin","users:write"}
    ScopeMatch match = ScopeMatch::All;
    // Optional: implication roots that satisfy this requirement even when the
    // exact scope is absent. e.g. an "admin" super-scope satisfies "users:read".
    std::vector<std::string> impliedBy;
};

class ResourceScopeRegistry {
  public:
    // Build from a JSON config block + the controller-declared EndpointInfo set.
    // Registered once at startup (registerBeginningAdvice), immutable after.
    static void registerRequirements(
        const std::vector<std::pair<std::string, std::string>>& routeDeclarations);

    // Returns nullptr when the path has no scope requirement (public / unconfigured).
    static const ResourceScopeRequirement* lookup(
        std::string_view path, drogon::HttpMethod method);

    // For OpenAPI/discovery: snapshot the full (path, method) -> scopes matrix.
    static std::vector<std::tuple<std::string, std::string, ResourceScopeRequirement>>
    snapshot();
};
}
```

**Path matching**: exact match first, then longest registered prefix with a `/` boundary
(preserves today's `/api/me/*` behavior but makes it explicit and queryable). No regex at the
scope layer — regex RBAC stays in `AuthorizationFilter`.

### 5.2 Where the registry is populated — three options (recommend A)

**(A) Controller-declared, via `EndpointInfo` (recommended).** Extend `EndpointInfo` with
`std::vector<std::string> requiredScopes` (+ optional `impliedBy`) and have each controller's
`initApiDocsImpl()` — which already enumerates paths and knows auth needs — declare them. At
startup, a single pass calls `ResourceScopeRegistry::registerRequirements(...)`. **Pros:** one
declaration site, drives both enforcement and OpenAPI, no separate config to drift. **Cons:**
requires touching each controller's `initApiDocsImpl` once.

**(B) Config-file driven (`config.json` `resource_scopes` block).** A JSON array of
`{path, method, scopes, match}`. **Pros:** no code change to add a scope; ops-tunable.
**Cons:** drifts from the code (the path strings must match `ADD_METHOD_TO` exactly); another
registry to keep in sync.

**(C) Hybrid — controller-declared with config override.** (A) as the base, (B) as an
optional override layer for hot-patching without rebuild. More moving parts; defer unless
needed.

**Recommendation:** Option (A). The whole point is a single authoritative source; the
controller already declares auth, and `initApiDocsImpl` is the place that maps 1:1 to routes.

### 5.3 Scope taxonomy: resource-prefixed read/write

Standardize on `<resource>:<action>` for resource scopes, while keeping the OIDC standard
scopes (`openid`, `profile`, `email`) untouched:

| Scope | Protects | Notes |
|---|---|---|
| `openid` | `/oauth2/userinfo` (OIDC Core §5.4) | unchanged |
| `profile`, `email` | `/oauth2/userinfo` claims; `/api/me` | unchanged |
| `users:read`, `users:write` | `/api/admin/users*` | splits the blanket `admin` |
| `clients:read`, `clients:write` | `/api/admin/clients*` | |
| `tokens:read`, `tokens:write` | `/api/admin/tokens*`, revoke | `write` covers revoke |
| `roles:read`, `roles:write` | `/api/admin/roles*`, `/scopes*` | |
| `audit:read` | `/api/admin/logs`, `/dashboard` | read-only |
| `admin` | **super-scope** — implies all `*:read`/`*:write` above | implication root |

The `read`/`write` rows seeded in `V006` become legacy aliases mapped to a default resource,
or are deprecated in favor of the prefixed forms. The OIDC scopes (`openid`/`profile`/`email`)
are **never** implied by `admin` — a user-info request still requires an actual user token
(RFC 6749 §3.3; this also matches the existing `client:`-subject rejection in
`TokenEndpointController.cc:1880`).

### 5.4 Scope implication (the missing layer)

Add an implication resolver consulted by the scope gate:

```cpp
// A token scope S satisfies requirement R if:
//   exact: S == R, OR
//   implied: R is in implies(S)
// e.g. implies("admin") ⊇ {users:read, users:write, clients:read, ...}
bool satisfies(const std::string& tokenScope,
               const ResourceScopeRequirement& req);
```

`impliedBy` is declared on each `ResourceScopeRequirement` (§5.2). The `admin` super-scope is
listed in every admin-resource requirement's `impliedBy`, so a token carrying only `admin`
still passes — no need to mint every leaf scope. Implication is **config-declared**, not
hardcoded, removing the triple-hardcoded list.

### 5.5 Retire the hardcoded `admin` list — use the DB column

Replace `IdentityService::scopeRequiresAdminRole` (the hardcoded vector) and
`ScopeDecisionEngine::isAdminScope` with a lookup against
`oauth2_scopes.requires_admin_role`. The 3-tier authorize-flow engine
(`ScopeDecisionEngine::evaluateScope`) reads the column instead of the constant vector. This
removes the drift risk flagged in fact #5 and makes admin-scope definition data-driven.

### 5.6 Enforcement — rewrite the filter scope gate

Both filters' scope-gate logic becomes a thin call into the registry + resolver:

```cpp
// OAuth2AuthFilter::doFilter, replacing lines 151-170
if (auto* req = ResourceScopeRegistry::lookup(req->path(), req->method())) {
    if (!authforge::drogon::authz::satisfies(tokenInfo->scope, *req)) {
        respondInsufficientScope(req, fcb, *req);   // single emitter (§5.7)
        return;
    }
}
```

`AuthorizationFilter` keeps its RBAC role check **after** the scope gate (ordering unchanged).
The hardcoded `requiredScopeForPath` / `requiredAdminScopeForPath` functions are deleted.

### 5.7 A single `insufficient_scope` emitter

One helper builds both the RFC 6750 `WWW-Authenticate` challenge (always including
`scope="..."`) and the body via `ErrorResponder` (`AUTHZ_INSUFFICIENT_PERMISSIONS` → 403).
The three current emitters (filters + `TokenEndpointController::userInfo`) call it. The
inline userinfo check is kept as defense-in-depth but routed through the same emitter so the
`scope` attribute is no longer omitted.

### 5.8 Discovery

- **Admin API**: `GET /api/admin/scopes/resources` returns the
  `ResourceScopeRegistry::snapshot()` matrix (path, method, required scopes, implied-by).
- **OpenAPI**: the existing per-operation `security` (#41) is supplemented with an
  `x-required-scopes` extension field populated from `EndpointInfo.requiredScopes`, so SDK
  consumers can bind scope requirements at codegen time.

---

## 6. Data model changes

Minimal. Add seed rows for the new resource scopes to `V006__oauth2_scopes.sql` (a new
migration `V0nn__resource_scopes.sql`; the existing file is idempotent `ON CONFLICT DO
NOTHING` so additive rows are safe). Set `requires_admin_role = true` on the admin family so
the DB column (not a constant) drives Tier-2. No new tables; no token-format change.

---

## 7. Backward compatibility

- A token carrying only `admin` continues to work (implication, §5.4).
- A token carrying the legacy `read`/`write` is grandfathered via alias mapping (§5.3) for at
  least one release, with a deprecation log the first time the alias fires.
- Existing OIDC flows (`openid`/`profile`/`email`) are unchanged.
- The error shape (`AUTHZ_INSUFFICIENT_PERMISSIONS`, HTTP 403) is unchanged; only the
  `WWW-Authenticate scope=` attribute becomes consistently present.

---

## 8. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Registry misses a route → 403 where there was none, or no gate where there was one | Build-time/startup consistency check: every route registered via `ADD_METHOD_TO` that carries an auth filter MUST have a registry entry; fail startup (LOG_FATAL) if not. Mirrors the `OAUTH2_AUTO_MIGRATE` loud-fail pattern. |
| Implication graph has a cycle | Resolver validates DAG at startup; reject cyclic config. |
| Operators rely on the blanket `admin` and don't mint leaf scopes | Implication makes `admin` still sufficient; no breakage. |
| OpenAPI `x-required-scopes` extension ignored by existing consumers | Additive; non-breaking for consumers that ignore unknown fields. |

---

## 9. Implementation plan (phased)

**Phase 1 — Foundation (no behavior change).**
1. Add `ResourceScopeRegistry` + scope-satisfies resolver (libs/drogon/authz).
2. Extend `EndpointInfo` with `requiredScopes`/`impliedBy`; populate from current hardcoded
   maps (so registry output == today's behavior).
3. Add startup consistency check.
4. Unit tests for the resolver (exact, implied, OIDC non-implication, empty match).

**Phase 2 — Switch enforcement.**
5. Rewrite both filters' scope gate to consult the registry; delete
   `requiredScopeForPath`/`requiredAdminScopeForPath`.
6. Consolidate the three `insufficient_scope` emitters into one helper.
7. Integration tests asserting the same 403/200 outcomes for existing routes (parity gate).

**Phase 3 — Granularity.**
8. Add resource-scope seed migration; declare `<resource>:read|write` on admin endpoints in
   each controller's `initApiDocsImpl`.
9. Retire the hardcoded `admin` list for the DB column (§5.5).
10. Parity + new per-resource 403 tests.

**Phase 4 — Discovery.**
11. `GET /api/admin/scopes/resources`; OpenAPI `x-required-scopes`; doc the matrix.

Each phase is independently shippable and reversible. **This design needs user sign-off before
Phase 1 implementation begins** (per the issue workflow, #43 is an architecture decision).

---

## 10. Open questions for the user

1. **Scope-prefix scheme**: `<resource>:<read|write>` as proposed, or a different convention
   (e.g. `<resource>.<read>`)? (Recommend `:`, matches the existing `admin:read` hint in
   `ScopeDecisionEngine.cc`.)
2. **Legacy `read`/`write`**: alias-map for one release then drop, or drop immediately?
3. **Registry source (§5.2)**: confirm Option A (controller-declared) over config-driven.
4. **Admin super-scope name**: keep `admin`, or introduce `admin:*` and treat bare `admin` as
   legacy?

These are decisions only the user can make; the recommended defaults above let implementation
proceed once confirmed.
