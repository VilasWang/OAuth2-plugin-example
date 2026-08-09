# Design: Postgres Storage + Redis Cache Layer (#42)

**Status:** Draft for review
**Author:** ZCode
**Created:** 2026-08-09
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

Introduce `RedisCached*Repository` decorators in a new library `libs/storage-cache/` (or under
`libs/storage-redis/`, reusing its Redis client access). Each implements its repository
interface and holds a `shared_ptr<I*Repository>` to the underlying Postgres impl.

```
                            OAuth2Plugin
                                 │
                    PostgresRepositoryBundle
                                 │ (decorated accessors)
            ┌────────────────────┴────────────────────┐
            │  RedisCachedTokenRepository             │
            │     └─ PostgresTokenRepository          │
            │  RedisCachedClientRepository            │
            │     └─ PostgresClientRepository         │
            └─────────────────────────────────────────┘
```

`PostgresRepositoryBundle` gains a `withCache(redisClientName)` factory that wraps each repo
in its decorator before returning it. Selection: when `config["cache"]["enabled"] == true`
(default false in Phase 1), the bundle wraps; otherwise it returns the plain Postgres impl.
**No interface change; no caller change.**

### 5.2 What to cache — and what NOT to (Phase 1)

| Repository | Read method | Cache? | Rationale |
|---|---|---|---|
| Client | `getClient` | **Yes** | Read-only (no client write path), high frequency at every token request. Mirrors the `CachedClientRepository` prototype. |
| Client | `validateClient` | **Yes** (delegates to getClient) | Same. |
| Token | `introspectToken` | **Yes, with care** | The hot path called on every protected-resource request. But revocation must invalidate (§5.4). |
| Token | `getAccessToken` | **Yes** | Bearer validation hot path; revoke invalidates. |
| Token | `getRefreshToken` | **No (Phase 1)** | Refresh-token reuse detection is the most security-sensitive path. Caching risks masking family invalidation. Defer until revocation invalidation is proven at scale. |
| Grant | `getAuthCode` | **No** | Auth codes are single-use, consumed in seconds — caching adds risk, no gain. |
| Consent | `hasUserConsent` | **Maybe (Phase 2)** | Consent changes are admin/user-triggered; write-through is feasible but low frequency. Defer. |

**Decision principle**: cache only where (a) the read is hot AND (b) every mutating path has
an unambiguous invalidation hook. Refresh tokens fail (b) for now.

### 5.3 Cache key layout & TTL

Keys are namespaced and use only non-sensitive identifiers (hashed tokens, client IDs).

```
authforge:cache:client:{clientId}              → JSON(OAuth2Client)   TTL 300s
authforge:cache:token:access:{sha256(token)}   → JSON(TokenIntrospection or active flag)  TTL = min(token_ttl, 60s)
authforge:cache:token:revoked:{sha256(token)}  → "1"  (negative cache, see §5.4)  TTL 600s
```

- **Client TTL**: 5 min (clients rarely change; no runtime write path today).
- **Access-token TTL**: the SHORTER of the token's remaining lifetime and a 60s cap. A token
  expiring in 10s is cached for 10s; a token expiring in 1h is cached for 60s (bounds staleness
  after a revocation the invalidator missed).
- **Negative cache**: when a token is explicitly revoked, set a short `revoked` marker so
  repeated introspections of a revoked token don't pound Postgres. The negative entry's TTL
  must not exceed the token's natural expiry (a revoked-and-expired token is irrelevant).

All values are JSON-serialized DTOs (same shape the standalone-Redis repos already serialize).
The raw token is never stored; only its hash appears in the key.

### 5.4 Invalidation semantics: write-through / write-invalidate

For every mutating method on a cached repository, the decorator invalidates (or updates) the
cache **after** the Postgres write succeeds, in the write's success callback.

| Write method (Token) | Cache action |
|---|---|
| `saveAccessToken` | no-op (a new token isn't cached until read) |
| `saveTokenPair` | no-op |
| `revokeAccessToken` | `DEL authforge:cache:token:access:{hash}` + `SET authforge:cache:token:revoked:{hash} 1` |
| `revokeRefreshToken` / `atomicRevokeRefreshToken` | (Phase 1: refresh not cached → no-op) |
| `revokeTokenFamily` | (Phase 1: refresh not cached; cascade also DELs matching access keys if feasible) |
| `incrementIntrospectCount` | **no cache write** — leave the cached introspection; the count is best-effort and read-modify-write. Document that `introspect_count` in a cached response may lag. |

**Race window**: between a Postgres COMMIT and the cache DEL there is a sub-millisecond window
where a concurrent reader could re-populate the cache with the now-stale value. Two mitigations:
1. The 60s TTL cap bounds the maximum staleness to 60s even if the DEL loses the race.
2. For revocation specifically, the **negative cache** is set before the DEL of the positive
   entry, so a re-population race is immediately corrected on the next read (the reader sees
   the `revoked` marker and does not serve).

This is **not** write-through-on-read-stale; it is write-invalidate + TTL + negative-cache.
Strong consistency would require a Postgres LISTEN/NOTIFY or transactional cache update, which
is out of scope (the audit's correctness bar is "no revoked token served as active"; the 60s
TTL + negative cache meets that with a bounded window).

### 5.5 Fallback (Redis unavailable) — transparent degrade

Every cache operation is wrapped:

```cpp
auto cb = std::make_shared<Callback>(std::move(userCb));
// try cache; on ANY miss/null/error → delegate to postgres impl with the same cb
redisClient_->execCommandAsync(
    [self, cb, req](const RedisResult&) {
        if (hit) (*cb)(deserialize(value));
        else self->impl_->readMethod(req, *cb);   // miss → postgres
    },
    [self, cb, req](const std::exception&) {
        self->impl_->readMethod(req, *cb);         // error → postgres (soft fail)
    }, "GET %s", key);
if (!redisClient_) self->impl_->readMethod(req, *cb);  // null → postgres
```

This matches the existing `if (!redisClient_)` soft-fail idiom and the cleanup-service
try/catch precedent. **A Redis outage must never cause a 500 or a wrong answer** — worst case
is a latency regression to Postgres-only.

### 5.6 Configuration

Add a `cache` block under the plugin config (additive; default off):

```json
"OAuth2Plugin": {
  "config": {
    "storage_type": "postgres",
    "cache": {
      "enabled": true,
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

`enabled: false` (default) keeps the system identical to today. Operators opt in per
deployment. Redis connection is reused (`redis_clients[0]`, already configured).

### 5.7 Observability

- Counters (prometheus) for cache hits/misses/errors per repository — the existing
  prometheus scrape already runs.
- A `/health/ready` sub-check: when cache is enabled, Redis reachability is reported (today it
  already is, via `HealthController.cc:112`); a cache-enabled deployment with Redis down is
  "degraded" not "down" (traffic still flows to Postgres).

---

## 6. Removing the deprecated standalone Redis mode

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
- The deprecated `storage_type="redis"` keeps working until §6 is executed (separate release).

---

## 8. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Revoked token served as active during the invalidate race | 60s TTL cap + negative-cache-before-DEL (§5.4). Add an integration test that revokes then immediately introspects and asserts not-active within the TTL bound. |
| `incrementIntrospectCount` drift masked by cache | Explicitly not cached; document the count is best-effort (it already is). |
| Redis outage causes latency spike | Soft-fail to Postgres (§5.5); the 10-connection pool already exists; monitor p99. |
| Cache stampede on a hot client key expiry | Single-flight in-flight reads (a per-key promise set) — Phase 2 optimization; the 5-min TTL makes this low-frequency. |
| Operator forgets to enable cache after Redis is up | No correctness impact (cache is an optimization); `enabled:false` is always safe. |

---

## 9. Implementation plan (phased)

**Phase 1 — Client cache (lowest risk, read-only).**
1. `RedisCachedClientRepository` implementing `IClientRepository`, wrapping the Postgres impl.
2. `PostgresRepositoryBundle::withCache(...)` factory; config `cache` block.
3. Soft-fail + TTL; prometheus counters.
4. Integration + contract tests (reuse the `tests/contract/` tiered pattern); a test that
   disables Redis mid-run and asserts Postgres fall-through.

**Phase 2 — Access-token / introspection cache.**
5. `RedisCachedTokenRepository` for `introspectToken` + `getAccessToken` only (refresh NOT
   cached).
6. Revocation invalidation + negative cache (§5.4).
7. Security integration test: revoke → introspect within TTL → assert not-active.

**Phase 3 — Remove deprecated standalone Redis mode (§6).** Breaking-change release.

**Phase 4 (optional) — Consent cache, L1+L2, single-flight.** Deferred; only if metrics
justify.

Each phase is independently shippable. **This design needs user sign-off before Phase 1
implementation begins** (#42 is an architecture decision).

---

## 10. Open questions for the user

1. **Phase-1 scope**: confirm caching only `IClientRepository` first (lowest risk), then
   access-token/introspection in Phase 2 — or go straight to the token cache given introspection
   is the hotter path?
2. **Library placement**: new `libs/storage-cache/`, or reuse `libs/storage-redis/` (its Redis
   client access is already there)? (Recommend `libs/storage-cache/` for clear separation, with
   a dependency on `libs/storage-redis` for the client base.)
3. **Default for `cache.enabled`**: ship off-by-default (safe, opt-in) or on-by-default for
   deployments that already configure Redis? (Recommend off-by-default in Phase 1.)
4. **Negative-cache TTL**: fixed 600s, or tied to the token's remaining lifetime?
   (Recommend `min(remaining_lifetime, 600s)`.)

These are decisions only the user can make; the recommended defaults above let implementation
proceed once confirmed.
