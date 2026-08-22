# [待发 issue] drogon session retention: every cookie-less request creates a Session held for session_timeout

> 状态：正文已备好 —— gh 凭据恢复后执行：
> `gh issue create --title "drogon session retention: every cookie-less request creates a Session held for session_timeout (memory + throughput tax)" --body-file docs/performance-optimization/upstream-drogon-session-issue.md`
> （2026-08-22：gh keyring 与 GITHUB_TOKEN 均失效待 `gh auth login`）

## Summary

Upstream drogon behavior ([drogonframework/drogon#278](https://github.com/an-tao/drogon/issues/278), closed as by-design): when `enable_session: true`, `HttpAppFrameworkImpl::findSessionForRequest` creates a **new Session for every request that arrives without a session cookie** and holds it in `SessionManager`'s CacheMap until `session_timeout`. Eviction itself works correctly (verified: live heap 106 MB → 15 MB after the TTL window).

For an OAuth2 authorization server this is expensive: machine/API traffic (token, introspect, userinfo, discovery — all cookie-less) pays per request.

## Measured impact (2026-08-22, evidence: `docs/performance-optimization/backend-memory-retention-investigation.md`)

| Effect | Measurement |
|---|---|
| Retention | **~1.1 KB per cookie-less request** until TTL (1.05 M requests → +1.1 GB, real build) |
| Steady-state formula | `API_QPS × session_timeout × ~1.1 KB` — 1k QPS @ 3600s ≈ 4 GB constant; 10k QPS @ 3600s ≈ OOM territory |
| Throughput tax (discovery) | **~-24%** (same-window controlled triple, real build, OFF/ON/OFF: 30.6k → 23.2k → 30.2k QPS) — applies at ANY TTL |
| Bench artifact | The 4-product comparison's "heaviest stack RSS 5,350 MiB" was this retention read after the S1 storm (backend 4.7 GB ≈ 5.4 M sessions) |

Session keys actually used by interactive flows: `userId/sub/auth_time/amr/mfa_*/webauthn_*` (8 keys, 4 interactive controllers only — zero use on machine endpoints).

## Current mitigation (delivered)

- Bench profile: `session_timeout: 30` (`config.bench.json`, e13041f) — caps retention at `QPS × 30 × 1.1 KB` (~2.8 GB at 85k QPS); interactive flows unaffected (write→read gaps are milliseconds; S4 login/authcode validated).
- Deployment guidance with the validated formula: `docs/ops/deployment.md` §性能调优.

## Root-fix options (blocked on upstream)

1. **Upstream lazy/per-path session creation** — the 4 interactive controllers are the only consumers; machine endpoints never read session state, so skipping creation when the request carries no session cookie AND the route never touches `req->session()` is semantically safe. Worth requesting upstream.
2. Drogon upgrade if lazy sessions land (currently 1.9.13 via conan).
3. (Rejected for now) Migrating the 8-key login state to a signed cookie — viable but touches auth-critical flows; revisit if the tax becomes production-relevant at scale.

## Production risk assessment

Retention is TTL-bounded (**no true leak** — eviction verified). Deployments with heavy API QPS should size `session_timeout` per the formula; interactive-only deployments are unaffected at 3600s.
