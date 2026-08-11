# Design: Postgres Storage + Redis Cache Layer (#42)

**Status:** Revised — Phase 1 implementation in progress
**Author:** ZCode
**Created:** 2026-08-09
**Revised:** 2026-08-11 (closed G1/G2/G3, folded S1–S5, added N1–N3 — see §11 + §12)
**Tracks:** GitHub issue #42 (successor to deprecated standalone Redis storage mode, F-005/#24)
**Related:** `oauth-oidc-compliance-audit.md` (F-005), `iam-architecture-audit.md`

---

## 1. Problem statement

The standalone `storage_type="redis"` mode never persisted refresh tokens, so rotation and
reuse-detection were silently non-functional. F-005 (#24) deprecated the mode: it now logs an
ERROR at startup (`OAuth2Plugin.cc:294-298`) and rejects the `refresh_token` grant with
`unsupported_grant_type` (`OAuth2Plugin.cc:454-478`).

The stated target architecture (named explicitly in the deprecation message) is:

- **Postgres is the single production storage backend** — source of truth for clients, grants,
  tokens, consents.
- **Redis returns only as a cache layer in front of Postgres** — hot read acceleration
  (client lookups, token introspection), with Postgres as the authoritative write path.
- **No standalone Redis storage mode** will be revived.

This design defines the cache layer: key layout, TTL strategy, invalidation semantics,
fallback behavior, configuration, and the migration path off the deprecated mode.

---

## 2. Goals & non-goals

**Goals**
- A Redis-backed cache **decorator** that wraps the Postgres repository impls, transparent to
  callers (the bundle returns the decorated interface).
- Cache the hottest read paths (client lookup, token introspection) without weakening the
  single-source-of-truth guarantee or token-reuse detection.
- Graceful, transparent degradation: if Redis is unavailable, every request falls through to
  Postgres with no error surfaced to the client.
- A documented, per-repository key layout and TTL policy.
- A clean removal path for `storage_type="redis"`.

**Non-goals**
- Caching **everything**. Tokens/grants are short-lived and churn-prone; only the
  demonstrably-hot, invalidation-safe paths are cached in Phase 1.
- Multi-tier caching (L1 process-local + L2 Redis). `CachedClientRepository` already prototyped
  an L1 (`drogon::CacheMap`); combining L1+L2 is a later optimization.
- Changing repository interfaces or the async-callback contract.
- Replacing the cleanup distributed-lock use of Redis (`OAuth2CleanupService`) — that stays.

---

## 3. RFC / standards basis

| Requirement | Basis |
|---|---|
| Introspection response shape & caching note | RFC 7662 §2.2 (introspection is a runtime lookup; the spec explicitly notes resource servers MAY cache, with the caveat in §4 on staleness) |
| Revocation immediate-effect expectation | RFC 7009 §2.1 — "invalidates" the token; a cache MUST NOT serve a revoked token |
| Refresh-token rotation / reuse detection | RFC 6749 §6, RFC 9700 §4.12.2 — reuse of a refresh token in a rotating flow MUST invalidate the family. A stale cache MUST NOT mask reuse detection. |

The hard correctness constraint: **a cache must never serve a token as active after it has
been revoked or after its family has been invalidated**, or it defeats the security guarantees
the audit fixed in F-003/F-004/F-005.

---

## 4. Current state (verified facts)

Summarized from the companion explore report (full file:line evidence there). Key facts:

- **Bundle seam is decorator-friendly.** `PostgresRepositoryBundle` accessors return
  `shared_ptr<I*Repository>` (interface type). A cache decorator implementing the same
  interface, wrapping the Postgres impl, plugs in with **zero caller changes**.
- **`CachedClientRepository`** (`libs/storage-redis/`) is a dormant, never-instantiated
  prototype of exactly this decorator shape — but it uses `drogon::CacheMap` (L1 in-process),
  not Redis. It is the template to copy.
- **Redis client pool already exists.** `drogon::app().getRedisClient("default")` is configured
  in `config.json:28-40` and already used by `OAuth2CleanupService` (distributed lock) and
  `HealthController` (probe). The cache reuses this pool — no new connection infrastructure.
- **Token is hashed before lookup.** `TokenService.cc:586-598` SHA-256-hashes the token before
  `introspectToken`. Cache keys MUST use `hash(token)`, never the raw token (and never log it).
- **`incrementIntrospectCount`** is a read-modify-write (`PostgresTokenRepository.cc:591-623`)
  that races any naive cache. It is best-effort today; the cache design must not make it worse.
- **Soft-fail precedent exists.** Every standalone-Redis repo method opens with
  `if (!redisClient_) { cb(<safe default>); return; }`. The cache decorator follows the same
  pattern: Redis null/error → fall through to Postgres.
- **No `IUserInfoRepository`** in the OAuth2 domain; user data is the separate identity domain
  (`authforge::identity::IUserRepository`), out of scope for this cache design unless the user
  asks.

---

## 5. Proposed design

### 5.1 Architecture: the cache decorator

Introduce `RedisCached*Repository` decorators in `libs/storage-redis/` (reusing its Redis
client access + DTO serialization infrastructure). Each implements its repository interface
and holds a `shared_ptr<I*Repository>` to the underlying Postgres impl.

```
                            OAuth2Plugin::initStorage
                                 │
                    PostgresRepositoryBundle (plain Postgres impls)
                                 │ (plugin wraps the client accessor in the decorator)
            ┌────────────────────┴────────────────────┐
            │  RedisCachedClientRepository            │   [Phase 1]
            │     └─ PostgresClientRepository         │
            │  RedisCachedTokenRepository             │   [Phase 2]
            │     └─ PostgresTokenRepository          │
            └─────────────────────────────────────────┘
```

**Wiring site (dependency-order constraint):** the decoration is applied in
`OAuth2Plugin::initStorage` (libs/drogon), NOT in `PostgresRepositoryBundle`. Reason: the root
`CMakeLists.txt` builds `libs/storage-postgres` (line 94) **before** `libs/storage-redis`
(line 110), so `storage-postgres` cannot depend on `storage-redis`. `libs/drogon` links BOTH,
so it is the correct layer to construct the decorator around the bundle's plain accessor.
The bundle itself stays dependency-clean (no `withCache` method); the plugin reads
`config["cache"]`, constructs the `RedisCachedClientRepository` around
`bundle.clientRepository()`, and passes the decorated handle into `assignOAuth2`.

Selection: when `config["cache"]["enabled"] == true` (default false in Phase 1), the plugin
wraps; otherwise it passes the plain Postgres impl through. **No interface change; no caller
change** — `assignOAuth2` takes `shared_ptr<IClientRepository>` either way.

### 5.2 What to cache — and what NOT to (Phase 1)

| Repository | Read method | Cache? | Rationale |
|---|---|---|---|
| Client | `getClient` | **Yes (Phase 1)** | Read-only (no client write path), high frequency at every token request. Mirrors the `CachedClientRepository` prototype. |
| Client | `validateClient` | **No (Phase 1)** | Secret validation is not safely cacheable (a cached "valid" could outlive a credential rotation). Pass-through to impl. *(Original draft listed this as "Yes (delegates to getClient)" — corrected: `validateClient` does NOT delegate to `getClient` in the Postgres/Redis impls; it is an independent secret-comparison path.)* |
| Token | `introspectToken` | **Phase 2, NOT Phase 1** (N2) | `PostgresTokenRepository::introspectToken` (`:500-589`) falls through to the **refresh_tokens table** if the access-token lookup misses. A blind cache of `introspectToken` under the `access:{hash}` key would therefore cache refresh-token introspections that `revokeRefreshToken` does not invalidate — a correctness hole. Phase 2 must add an access-vs-refresh discriminator (e.g. cache only after a confirmed `getAccessToken` hit) before this path is cacheable. |
| Token | `getAccessToken` | **Phase 2** (was Phase 1) | Bearer validation hot path; access-token-only by construction (no refresh-token fallthrough), so it is safely cacheable. Moved to Phase 2 to keep Phase 1 strictly client-cache (lowest risk, exercises the full decorator + soft-fail + observability chain before touching token-revocation semantics). Revoke invalidates via §5.4. |
| Token | `getRefreshToken` | **No (Phase 1)** | Refresh-token reuse detection is the most security-sensitive path. Caching risks masking family invalidation. Defer until revocation invalidation is proven at scale. |
| Grant | `getAuthCode` | **No** | Auth codes are single-use, consumed in seconds — caching adds risk, no gain. |
| Consent | `hasUserConsent` | **Maybe (Phase 2)** | Consent changes are admin/user-triggered; write-through is feasible but low frequency. Defer. |

**Decision principle**: cache only where (a) the read is hot AND (b) every mutating path has
an unambiguous invalidation hook (or a TTL-bounded convergence window, §10.7). Refresh tokens
fail (b) for now; `introspectToken` fails (b) in Phase 1 because of its access/refresh
fallthrough (N2). `getAccessToken` is deferred to Phase 2 only to keep Phase 1 minimal.

### 5.3 Cache key layout & TTL

Keys are namespaced and use only non-sensitive identifiers (hashed tokens, client IDs).
Phase 1 implements only the **client** key; the token keys are shown for Phase 2 completeness.

```
authforge:cache:client:{clientId}                 → JSON(OAuth2Client)   TTL 300s            [Phase 1]
authforge:cache:token:access:{sha256(token)}      → JSON(OAuth2AccessToken)  TTL = min(token_ttl, 60s)  [Phase 2]
authforge:cache:token:revoked:{sha256(token)}     → "1"  (negative cache, see §5.4)  TTL see below       [Phase 2]
```

- **Client TTL**: 5 min (clients rarely change; no runtime write path today). **Phase 1.**
- **Access-token TTL** (Phase 2): the SHORTER of the token's remaining lifetime and a 60s cap.
  A token expiring in 10s is cached for 10s; a token expiring in 1h is cached for 60s (bounds
  staleness after a revocation the invalidator missed).
- **Negative cache TTL** (Phase 2): `min(remaining_lifetime, 600s)` where `remaining_lifetime
  = max(0, token.exp - now)` — closes G2 (the original fixed `600s` exceeded most access-token
  lifetimes, contradicting §5.4). **Exception (N3):** the `revokeAccessToken(token, revokedBy,
  cb)` entry point receives only the **hashed token string**, not the token's `exp`, so the
  decorator cannot compute `remaining_lifetime` at revoke time. For access-token negative
  entries the TTL is therefore a **fixed 60s** (matching the access-token cache cap) —
  justified because 60s ≤ any reasonable access-token lifetime and the negative cache's job is
  only to shed load during a revoke storm, not to be precise. The `min(remaining_lifetime,
  600s)` formula remains the target for any future revoke path that carries the exp.

All values are JSON-serialized DTOs (same Json::StreamWriter/CharReader pattern the
standalone-Redis repos already use). The raw token is never stored; only its hash appears in
the key (and the hash is never logged).

### 5.4 Invalidation semantics: write-through / write-invalidate

For every mutating method on a cached repository, the decorator invalidates (or updates) the
cache **after** the Postgres write succeeds, in the write's success callback. The table below
covers the **Phase 2 token** paths (Phase 1 = client cache has no mutating path, so there is
no invalidation in Phase 1 — see the "Client write path" row).

| Write method (Token) | Cache action |
|---|---|
| `saveAccessToken` | no-op (a new token isn't cached until read) |
| `saveTokenPair` | no-op |
| `revokeAccessToken` | `SET authforge:cache:token:revoked:{hash} 1 EX 60` **before** `DEL authforge:cache:token:access:{hash}` (negative-cache-before-DEL ordering, see Race window below). The revoked entry's 60s TTL is the N3 exception (§5.3). Applies to **both** `introspectToken` and `getAccessToken` read paths — they share the same `access:{hash}` / `revoked:{hash}` key pair, so the action is stated once here (S2). |
| `revokeRefreshToken` / `atomicRevokeRefreshToken` | (Phase 2: refresh not cached → no-op) |
| `revokeTokenFamily` | **(b) TTL-bounded convergence** (G1 closure, §10.5). The decorator does **not** maintain a `familyId → {access-hash}` index in Phase 2. Family-revoked access tokens remain cacheable until the 60s access-token TTL cap bounds the staleness window. This still meets the "no revoked token served as active" bar — the worst case is a ≤60s window where a family-revoked access token may be served as active, after which the TTL expiry + the negative-cache set by any subsequent `revokeAccessToken` corrects it. Revisit option (a) (explicit `family:{id}` → hash set, populated on `saveTokenPair`, consumed on family revoke) only if metrics show meaningful family-revoke traffic. |
| `incrementIntrospectCount` | **no cache write** — leave the cached introspection; the count is best-effort and read-modify-write. Document that `introspect_count` in a cached response may lag. |
| **Client write path (admin mutations)** | **Runtime write paths DO exist** (correction: the original draft wrongly claimed "none exists today"). `ClientManagementService` (`libs/drogon/src/admin/ClientManagementService.cc`) exposes 4 admin mutation paths that operate directly on the `Oauth2Clients` ORM table, bypassing the `IClientRepository` interface: `updateClient` (`:280`), `resetClientSecret` (`:414`), `updateClientScopes` (`:517`), `deleteClient` (`:373`). **When `cache.enabled=true`, each of these MUST invalidate `authforge:cache:client:{clientId}` in its Postgres-write success callback** — otherwise a removed redirect URI / scope / secret / client is served stale from cache for up to the 300s client TTL. **Phase-1 implementation gap (PR #47 review, Codex P1):** invalidation hooks are NOT yet wired into `ClientManagementService`. Two mitigation options until they land: (a) keep `cache.enabled=false` by default (already the case — the staleness window only exists for operators who explicitly opt in), and (b) document in the config block + ops runbook that enabling the client cache requires these invalidation hooks. The hooks are a tracked follow-up: add a `DEL authforge:cache:client:{clientId}` (fire-and-forget, best-effort, silent on Redis error) to each of the 4 success callbacks. This is a **gap against the "no stale client served" bar**, bounded by the 300s TTL — same correctness class as the token-revoke window (§5.4), not a Phase-1 blocker because the cache is off by default. |

**Pre-hashed tokens (G3 closure):** the revoke entry points receive **already-hashed** tokens.
Verified: `TokenService::revokeAccessToken` (`.cc:612`) does `auto hashedToken =
hashToken(*crypto_, token);` *before* calling `tokens_->revokeAccessToken(hashedToken, ...)`,
exactly mirroring the introspect path (`.cc:596`). The decorator therefore builds the
invalidation key directly from its already-hashed argument — **no extra hashing** is needed,
and there is no raw/hash mismatch risk.

**Race window**: between a Postgres COMMIT and the cache DEL there is a sub-millisecond window
where a concurrent reader could re-populate the cache with the now-stale value. Two mitigations:
1. The 60s TTL cap bounds the maximum staleness to 60s even if the DEL loses the race.
2. For revocation specifically, the **negative cache** is set **before** the DEL of the positive
   entry, so a re-population race is immediately corrected on the next read (the reader sees
   the `revoked` marker and does not serve).

This is **not** write-through-on-read-stale; it is write-invalidate + TTL + negative-cache.
Strong consistency would require a Postgres LISTEN/NOTIFY or transactional cache update, which
is out of scope (the audit's correctness bar is "no revoked token served as active"; the 60s
TTL + negative cache meets that with a bounded window — see §10.7 for the formal
bounded-eventual-consistency declaration).

### 5.5 Fallback (Redis unavailable) — transparent degrade

Every cache operation is wrapped. The critical correctness constraint (S1) is that the user
callback fires **exactly once** even under partial Redis failure: drogon's async success and
error callbacks could both fire on a torn connection, so the soft-fail path MUST guard with a
`shared_ptr<std::atomic<bool>> fired`:

```cpp
auto cb = std::make_shared<Callback>(std::move(userCb));
auto fired = std::make_shared<std::atomic<bool>>(false);

if (!redisClient_) {
    impl_->readMethod(req, *cb);                  // null client → postgres
    return;
}
redisClient_->execCommandAsync(
    [self, cb, fired, req](const RedisResult& res) {
        if (res.type() == kString || res.type() == kArray) {   // hit
            if (!fired->exchange(true)) (*cb)(deserialize(res));
        } else {                                                // miss (kNil etc.)
            if (!fired->exchange(true)) self->impl_->readMethod(req, *cb);
        }
    },
    [self, cb, fired, req](const std::exception&) {
        if (!fired->exchange(true))                             // error → postgres (soft fail)
            self->impl_->readMethod(req, *cb);
    }, "GET %s", key);
```

This matches the existing `if (!redisClient_)` soft-fail idiom and the cleanup-service
try/catch precedent. **A Redis outage must never cause a 500, a wrong answer, or a double
callback invocation** — worst case is a latency regression to Postgres-only. The `atomic<bool>`
once-guard is a **mandatory** Phase-1 implementation constraint (S1).

### 5.6 Configuration

Add a `cache` block under the plugin config (additive; default off):

```json
"OAuth2Plugin": {
  "config": {
    "storage_type": "postgres",
    "cache": {
      "enabled": false,
      "redis_client_name": "default",
      "ttl_seconds": {
        "client": 300,
        "access_token_max": 60
      }
    },
    ...
  }
}
```

- `enabled: false` (default) keeps the system identical to today. Operators opt in per
  deployment.
- **`redis_client_name` actually drives `getRedisClient`** (S3): the `withCache` factory calls
  `drogon::app().getRedisClient(config["cache"]["redis_client_name"])` rather than hardcoding
  `"default"`. This both honors the config key (closing S3's "dead config" risk) and lets
  operators point the cache at a **dedicated Redis instance** separate from the one backing
  `OAuth2CleanupService`'s distributed lock (§10.6 co-tenancy decision). Default `"default"`
  preserves co-tenancy for zero-config upgrades.
- Redis connection is reused (the `redis_clients[]` array already configured in `config.json`).
  The cache adds no new connection infrastructure.

### 5.7 Observability

- **Counters via the `IMetrics` port** (N1). The decorator receives a
  `shared_ptr<authforge::common::ports::IMetrics>` in its constructor (the same
  injection pattern `TokenService` uses for `IAuditSink`), constructed once in
  `OAuth2Plugin::initAndStart()` as `DrogonMetrics` (the only production `IMetrics`
  impl today — it emits `LOG_INFO` lines, **not** Prometheus, verified at
  `DrogonMetrics.cc:33-76`). Counter name `authforge_cache_total`, labels
  `repo=client|token` and `outcome=hit|miss|error`. The port abstraction means a
  future PromExporter-backed `IMetrics` impl picks these counters up with zero
  decorator changes. *(Original draft said "prometheus counters" — corrected to
  match the verified, currently-log-emitting IMetrics reality.)*
- A `/health/ready` sub-check: when cache is enabled, Redis reachability is reported (today it
  already is, via `HealthController.cc:112`); a cache-enabled deployment with Redis down is
  "degraded" not "down" (traffic still flows to Postgres via §5.5 soft-fail).

---

## 6. Removing the deprecated standalone Redis mode — ⚠️ BREAKING CHANGE

**⚠️ BREAKING (Phase 3, separate release):** this removal refuses startup on
`storage_type="redis"`. It is a major-version breaking config change. Release-note it
prominently and add a startup guard that fails fast with a clear migration message.

Once the cache layer ships and `storage_type="postgres" + cache.enabled=true` is validated:
1. Remove the `redis` branch in `OAuth2Plugin::initStorage` (`:285-327`).
2. Remove the `storageType_ == "redis"` reject in `refreshAccessToken` (`:454-478`).
3. Remove the deprecation ERROR log (`:294-298`).
4. Keep `libs/storage-redis/` — its Redis client access + DTO serialization are reused by the
   cache decorator; only the standalone-storage `*Repository` impls (and the no-op refresh
   methods) are deleted.

This is a **breaking config change** for any deployment still on `storage_type="redis"`; it
requires a release-note + a migration check that refuses startup on the removed value.

---

## 7. Backward compatibility

- `cache.enabled` defaults to **false**: existing deployments see no change.
- No interface or DTO changes; token/client JSON shapes unchanged.
- The deprecated `storage_type="redis"` keeps working until §6 is executed (**Phase 3,
  BREAKING** — see §6 for the startup-refusal + release-note requirement).

---

## 8. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Revoked token served as active during the invalidate race | 60s TTL cap + negative-cache-before-DEL (§5.4). Add an integration test that revokes then immediately introspects and asserts not-active within the TTL bound. |
| **`revokeTokenFamily` cascade leaves access tokens cacheable (G1)** | Accepted as **TTL-bounded convergence** (§10.5): no `familyId → hash` index in Phase 2. Worst case is a ≤60s window where a family-revoked access token may be served as active; the 60s TTL cap + any subsequent `revokeAccessToken` negative-cache corrects it. Meets the "no revoked token served as active" bar with a bounded window. Add an integration test that revokes a family then introspects the access token and asserts not-active after the TTL. |
| **Cache↔cleanup-lock co-tenancy (S5)** | A cache stampede (hot-key expiry) could exhaust the shared Redis connection pool or OOM the instance, degrading `OAuth2CleanupService`'s distributed lock (which uses the same `redis_clients[0]`). Mitigations: (1) `redis_client_name` (§5.6/§10.6) lets operators point the cache at a **dedicated instance** to fully isolate the lock; (2) the 10-connection pool + soft-fail bounds cache-induced pressure; (3) the 5-min client TTL makes stampedes low-frequency. Document in the ops runbook that production deployments with high RPS SHOULD set a dedicated `redis_client_name` for the cache. |
| `incrementIntrospectCount` drift masked by cache | Explicitly not cached; document the count is best-effort (it already is). |
| Redis outage causes latency spike | Soft-fail to Postgres (§5.5); the 10-connection pool already exists; monitor p99. |
| Cache stampede on a hot client key expiry | Single-flight in-flight reads (a per-key promise set) — Phase 2 optimization; the 5-min TTL makes this low-frequency. |
| Operator forgets to enable cache after Redis is up | No correctness impact (cache is an optimization); `enabled:false` is always safe. |
| **Soft-fail double-callback (S1)** | `shared_ptr<atomic<bool>> fired` once-guard in every soft-fail path (§5.5). Mandatory Phase-1 constraint; covered by a unit test. |
| **Admin client mutations bypass the cache (PR #47 review, Codex P1)** | `ClientManagementService::{updateClient,resetClientSecret,updateClientScopes,deleteClient}` mutate `Oauth2Clients` directly via the ORM, NOT through `IClientRepository`, so the decorator's invalidation hook is never reached. A deleted/modified client is served stale from cache for up to the 300s client TTL. **Phase-1 mitigation: `cache.enabled` defaults to `false`** — the staleness window only opens when an operator explicitly opts in. **Follow-up:** add a fire-and-forget `DEL authforge:cache:client:{clientId}` to each of the 4 admin success callbacks (tracked gap, §5.4). Documented in config.json + ops runbook. |

---

## 9. Implementation plan (phased)

**Phase 1 — Client cache (lowest risk, read-only). ← THIS RELEASE**
1. `RedisCachedClientRepository` implementing `IClientRepository`, wrapping the Postgres impl.
   Cache key `authforge:cache:client:{clientId}`, value JSON-serialized `OAuth2Client`,
   TTL 300s.
2. `PostgresRepositoryBundle::withCache(redisClient, metrics, ttlConfig)` factory; config
   `cache` block (§5.6).
3. Soft-fail + TTL + `atomic<bool>` once-guard (§5.5/S1); `IMetrics` counters (§5.7/N1).
4. Integration tests (hit, miss, soft-fail null-client, nullopt-not-cached, validateClient
   pass-through); a test that disables Redis mid-run and asserts Postgres fall-through.

**Phase 2 — Access-token cache (getAccessToken first, then introspectToken).**
5. `RedisCachedTokenRepository` for `getAccessToken` only (access-token-only by construction,
   no refresh fallthrough — safe).
6. Revocation invalidation + negative cache (§5.4). `revokeAccessToken` sets
   `token:revoked:{hash} 1 EX 60` then DELs `token:access:{hash}`.
7. **Then** add `introspectToken` caching **with the N2 access-vs-refresh discriminator**
   (cache only after a confirmed access-token lookup, never the refresh-token fallthrough).
8. Security integration test: revoke → introspect within TTL → assert not-active. Family-revoke
   convergence test: revokeTokenFamily → introspect access token → assert not-active after TTL.

**Phase 3 — Remove deprecated standalone Redis mode (§6).** ⚠️ BREAKING-change release.

**Phase 4 (optional) — Consent cache, L1+L2, single-flight.** Deferred; only if metrics
justify.

Each phase is independently shippable. **Sign-off obtained (2026-08-11):** all decisions in
§10 (including the §12 N1–N3 additions) are confirmed; Phase 1 implementation may begin
(#42 is an architecture decision).

---

## 10. Decisions (confirmed 2026-08-11)

All items below were open questions; the user approved them on 2026-08-11. Items marked
**[review]** were raised by the §11 correctness review and are not in the original draft. The
chosen option is the one recommended in the review — each is the industry-standard / lowest-risk
default.

### Original open questions

1. **Phase-1 scope — DECIDED: A (client first).** Cache only `IClientRepository` first (lowest
   risk, no invalidation path), then access-token/introspection in Phase 2. B (token-first) is
   explicitly **out of scope for Phase 1**: client caching is the industry-standard "safe-to-cache"
   first step (static/low-churn config like clients, JWKS, certs) and exercises the full
   decorator + invalidation + soft-fail + observability chain without touching any
   security-revocation semantic. Introspection's 60s TTL cap already bounds staleness, so
   deferring it is a latency miss, not a correctness risk.
2. **Library placement — DECIDED: new `libs/storage-cache/`.** Clear separation from
   `libs/storage-redis/`; depends on it only for the Redis client base.
3. **Default for `cache.enabled` — DECIDED: off-by-default** (safe, opt-in) in Phase 1.
4. **Negative-cache TTL — DECIDED: `min(remaining_lifetime, 600s)`.** Also closes review gap G2
   (the fixed 600s in §5.3 must be corrected to this formula).

### Added by §11 correctness review **[review]**

5. **`revokeTokenFamily` cache semantics (G1) — DECIDED: (b) TTL-bounded convergence.** The
   decorator will **not** maintain a `familyId → {access-hash}` index in Phase 1. Family-revoked
   access tokens remain cacheable until the 60s TTL cap bounds staleness, which still meets the
   "no revoked token served as active" bar. Revisit (a) (explicit `family:{id}` → hash set) only
   if metrics show meaningful family-revoke traffic. (Industry-standard: avoid new write-path
   state for an edge-case invalidation path; rely on bounded-TTL convergence.)
6. **Redis instance co-tenancy (S5) — DECIDED: (a) expose a dedicated knob.** `cache` config gains
   its own `redis_client_name` (default `"default"`, i.e. co-tenancy preserved for zero-config
   upgrades); operators **may** point it at a separate Redis instance to isolate cache pressure
   from the `OAuth2CleanupService` distributed lock. Low-cost because S3 already makes the name
   config-driven. §8 must list the co-tenancy risk row regardless of choice.
7. **Correctness-boundary declarations — DECIDED: approved.** Add two scheme-level statements:
   (i) this design provides *bounded eventual consistency* (staleness ≤ TTL cap), not strong
   consistency — strong consistency would need LISTEN/NOTIFY or transactional cache update, out
   of scope; (ii) refresh-token caching stays out of Phase 1, revisited only if metrics justify
   (principle: an entity is cacheable only if every invalidation path has either an explicit hook
   or a TTL-bounded convergence window).

All decisions confirmed; the recommended defaults above are now the locked design.

### Added by §12 implementation review **[impl]**

8. **Metrics emission — DECIDED: IMetrics port, not Prometheus (N1).** Verified: the only
   production `IMetrics` implementation is `DrogonMetrics` (log-emitting, no PromExporter).
   The decorator therefore emits counters via `incrementCounter("authforge_cache_total",
   {{"repo","..."},{"outcome","..."}})` through the injected `shared_ptr<IMetrics>`, matching
   the existing injection pattern. A future PromExporter-backed impl picks these up with zero
   decorator changes.
9. **`introspectToken` cache scope — DECIDED: Phase 2 only, after an access-vs-refresh
   discriminator (N2).** `PostgresTokenRepository::introspectToken` (`:500-589`) falls through
   to the refresh_tokens table on access-token miss. A blind cache under the `access:{hash}`
   key would cache refresh-token introspections that `revokeRefreshToken` does not invalidate.
   Phase 1 does NOT cache tokens at all; Phase 2 adds `getAccessToken` first (safe), then
   `introspectToken` only with a confirmed-access-token discriminator.
10. **Negative-cache TTL for access tokens — DECIDED: fixed 60s (N3).** The
    `revokeAccessToken(token, revokedBy, cb)` entry point receives only the hashed token, not
    the exp, so `min(remaining_lifetime, 600s)` (§10.4) cannot be computed. The 60s fixed TTL
    matches the access-token cache cap and is ≤ any reasonable access-token lifetime. The
    formula remains the target for any future revoke path that carries the exp.

All decisions confirmed; the recommended defaults above are now the locked design.

---

## 11. Objective correctness review (2026-08-11)

A fact-check pass verified the file:line claims in §1/§4 against the actual source tree:
12 claims checked, 11 FOUND-CORRECT and 1 PARTIALLY-CORRECT (config.json:28-40 is correct in
substance, only the line-range boundary is slightly wide — it should read `redis_clients[0]`
or `config.json:28-40` for the array). No interface method-name mismatches were found
(`revokeAccessToken` / `revokeRefreshToken` are the real names; the doc's worry about a
`revokeToken` alias is unfounded). The factual basis of this design is sound.

The following correctness issues were then raised against the *design logic itself* and
re-verified against the implementation (`TokenService.cc`, `PostgresTokenRepository.cc`,
`OAuth2Plugin.cc`). Each item states its verification result.

### 11.1 🔴 Hard gaps (must be closed before sign-off)

**G1 — `revokeTokenFamily` has no cache-invalidation hook (the "if feasible" gap).**
`PostgresTokenRepository::revokeTokenFamily` (`:452-496`) cascades via a SQL sub-query
`WHERE token IN (SELECT access_token FROM oauth2_refresh_tokens WHERE family_id = $1)`.
The cache key layout (§5.3) only has `access:{sha256(token)}`; there is **no index mapping
`familyId → {access-token hashes}`**. The §5.4 table therefore lists the action as
"(refresh not cached; cascade also DELs matching access keys **if feasible**)" — this is an
unresolved blank, not a design. Two concrete options, pick one and document it:
- (a) The decorator maintains `authforge:cache:family:{familyId}` → Redis set of access-hash
      keys, populated on `saveTokenPair` and consumed (DEL + negative-cache) on family revoke.
- (b) Accept that family-revoked access tokens remain cacheable until the 60s TTL cap bounds
      the staleness, and state this honestly in §5.4 (the 60s cap still meets the
      "no revoked token served as active" bar, just with a bounded window).
Current text punts with "if feasible" — not acceptable for an architecture decision.

**G2 — Negative-cache TTL is self-contradictory between §5.3 and §10.**
§5.3 states a **fixed 600s** negative-cache TTL; §10 open question 4 recommends
`min(remaining_lifetime, 600s)`. §5.4 itself says the negative entry "must not exceed the
token's natural expiry". A fixed 600s will exceed most access-token lifetimes (default <<
600s), contradicting §5.4. **Resolution:** adopt `min(remaining_lifetime, 600s)` everywhere;
fix §5.3 to drop the fixed 600s.

**G3 — [REVIEWED & WITHDRAWN]** Original concern: if the revoke path passed a *raw* token
while cache keys use `hash(token)`, every `DEL`/negative-cache write would miss and the
revocation closure would silently fail. **Verification overturns this:** `TokenService::
revokeAccessToken` (`.cc:612`) does `auto hashedToken = hashToken(*crypto_, token);` *before*
calling `tokens_->revokeAccessToken(hashedToken, ...)`, exactly mirroring the introspect path
(`.cc:596`). The cache decorator can therefore build the invalidation key directly from the
already-hashed argument. **No change required** — but §5.4 should state explicitly that the
revoke entry points receive pre-hashed tokens so the decorator needs no extra hashing.

### 11.2 🟡 Should-fix (boundary constraints for Phase 1 implementation)

**S1 — Callback must fire exactly once in the soft-fail path (§5.5).** The §5.5 pseudo-code
reuses one `shared_ptr<Callback>` across both the cache-hit branch and the fallback branch.
Under a partial Redis failure the drogon async success *and* error callbacks could both fire,
invoking `cb` twice — violating the async-callback contract the design promises to preserve
(§2). Require an `std::atomic<bool>` / `once_flag` guard so `cb` runs exactly once.

**S2 — Unify invalidation for `introspectToken` and `getAccessToken`.** Both share the same
`access:{hash}` key; §5.4 should list the revoke actions once and apply them to both read
paths, not imply they are separately handled.

**S3 — `redis_client_name` must actually drive `getRedisClient`.** §5.6 exposes
`redis_client_name`, but §5.5/§5.1 prose says "reuse `redis_clients[0]`". The `withCache`
factory must call `getRedisClient(config["cache"]["redis_client_name"])` rather than hardcoding
`"default"`, or the config key is dead.

**S4 — Mark §6 as a breaking change prominently.** §7 says the deprecated mode "keeps working
until §6 is executed"; §6 is a startup-refusing breaking change. State the breaking level
explicitly in both §6 and §7.

**S5 — (Scheme-level) Cache and distributed-lock share one Redis instance — declare the
coupling risk.** §5.1/§5.6 reuse `redis_clients[0]` (the same `"default"` client already used by
`OAuth2CleanupService` for its distributed lock and by `HealthController` for probing). At the
scheme level this conflates two very different criticality classes on one resource:
- The **cache** is latency-sensitive and data-loss-tolerant (any miss degrades to Postgres via
  the §5.5 soft-fail).
- The **cleanup distributed lock** is correctness/availability-sensitive: if Redis is saturated,
  OOM-killed, or its connection pool is exhausted by a cache stampede, the lock can fail or
  contend, potentially allowing concurrent cleanup runs.
The doc currently assumes "the 10-connection pool already exists; soft-fail covers outages"
(§8) but only lists cache-local failure modes. **Required:** add to §8 an explicit risk row for
the cache↔lock co-tenancy (cache pressure degrading the lock), and either (a) recommend a
dedicated `redis_client_name` for the cache (so operators *can* separate instances), or (b)
state that co-tenancy is accepted and bounded by the existing pool + soft-fail. Note S3 already
makes `redis_client_name` config-driven, so (a) is low-cost — but the scheme must say so.

### 11.3 Factual errata (cosmetic)

- §4 claim 4 (`config.json:28-40`): behavior correct, range slightly wide — prefer
  `redis_clients[0]` (default client at `:29-39`).
- §4 claim 3 ("dormant / never-instantiated"): `CachedClientRepository` *exists* in
  `libs/storage-redis/` and is instantiated only by tests; production wiring never connects it.
  Rephrase as "only test-instantiated, not wired into production", not "does not exist".

### 11.4 Verdict

Facts are trustworthy and the decorator + write-invalidate + TTL-cap + negative-cache direction
is sound. Before user sign-off, **close G1 and G2** (G3 is withdrawn after code verification),
and fold S1–S4 in as mandatory Phase-1 implementation constraints.

**Status (2026-08-11 revision):** all of G1/G2/G3 + S1–S5 are now closed in §5/§8/§10 above.
§11 is preserved as the historical review record.

---

## 12. Implementation review (2026-08-11)

A second pass, conducted while preparing the Phase-1 implementation, re-verified the §11
findings against source AND raised three additional issues (N1–N3) that the original §11
review did not catch. All three are now locked decisions in §10 (items 8–10) and propagated
into §5. The findings:

### 12.1 Additional correctness issues (all closed)

**N1 — Metrics: "prometheus counters" claim is unverified (§5.7).** The §5.7 original text
claimed "Counters (prometheus) for cache hits/misses/errors ... the existing prometheus
scrape already runs." **Verification:** the only production `IMetrics` implementation is
`authforge::drogon::adapters::DrogonMetrics` (`libs/drogon/src/adapters/DrogonMetrics.cc:33-76`),
which emits `LOG_INFO` lines — it does **not** touch `drogon::plugin::PromExporter`. There is
no PromExporter-backed `IMetrics` impl in the tree. The legacy
`authforge::drogon::observability::Metrics` static class also only logs, despite an unused
`#include <drogon/plugins/PromExporter.h>`. **Resolution (§10.8):** emit via the `IMetrics`
port through the existing injection pattern; the doc text is corrected in §5.7. A future
PromExporter-backed impl is a separate, deferred task (documented in `IMetrics.h:11-23`).

**N2 — `introspectToken` fallthrough to refresh tokens breaks the access:{hash} cache key
(§5.2/§5.4).** `PostgresTokenRepository::introspectToken` (`libs/storage-postgres/src/
PostgresTokenRepository.cc:500-589`) first looks up `oauth2_access_tokens`, and on miss
**falls through to `oauth2_refresh_tokens`** (lines 548-587). The cache key layout (§5.3)
keys both on `sha256(token)` with no type discriminator. **Consequence:** calling
`introspectToken` with a refresh-token value would cache the refresh-token introspection
under `authforge:cache:token:access:{hash}`, but `revokeRefreshToken` only clears refresh
state — it would never DEL that key. A revoked refresh token could then be served as
`active=true` from cache. **Resolution (§10.9):** Phase 1 does NOT cache tokens at all.
Phase 2 adds `getAccessToken` first (access-token-only by construction — no refresh
fallthrough — so unconditionally safe), then `introspectToken` only with an explicit
access-vs-refresh discriminator (cache only on a confirmed access-token lookup). §5.2/§5.4
updated.

**N3 — `revokeAccessToken` lacks the token exp needed for `min(remaining_lifetime, 600s)`
(§5.3/§5.4).** The §10.4 formula `min(remaining_lifetime, 600s)` requires the token's `exp`.
But `ITokenRepository::revokeAccessToken(const std::string &token, const std::string
&revokedBy, VoidCallback &&cb)` receives only the hashed token string — verified at
`libs/oauth2/include/authforge/oauth2/repository/ITokenRepository.h:141-145`. The decorator
cannot compute `remaining_lifetime` at revoke time without an extra Redis GET to read the
cached `exp` first (a read-before-write that adds latency and itself races). **Resolution
(§10.10):** access-token negative entries use a **fixed 60s TTL** (matching the access-token
cache cap of §5.3). Justified: 60s ≤ any reasonable access-token lifetime, the negative
cache's purpose is load-shedding during a revoke storm (not precision), and the formula
remains the target for any future revoke path that carries the exp. §5.3/§5.4 updated with
the documented exception.

### 12.2 Phase-1 scope confirmation

Phase 1 is **client-cache only** (§9, §10.1). None of the token-revocation concerns (G1/N2/N3)
are reachable in Phase 1 because no token path is cached. This makes Phase 1 strictly a
read-only, no-invalidation-path exercise — the safest possible first step, exactly as §10.1
intended. All Phase-2 token concerns are addressed in the design so Phase 2 has no open
questions when it starts.

### 12.3 Verdict

The design is now implementation-ready for Phase 1. G1/G2/G3 + S1–S5 (from §11) and N1–N3
(from §12) are all closed; §10 records the locked decisions.
