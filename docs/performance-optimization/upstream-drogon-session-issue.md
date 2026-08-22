# [待发 issue] drogon session retention: every cookie-less request creates a Session held for session_timeout

> 状态：2026-08-22 尝试提交被拒 —— 当前 GITHUB_TOKEN 是细粒度 PAT（仅授权本仓库），
> 对 drogonframework/drogon 报 `Resource not accessible by personal access token (createIssue)`。
> 解法（二选一）：① 换 classic PAT（`public_repo` scope）或细粒度 PAT 选 "All repositories" + Issues 写权限；
> ② 取消 GITHUB_TOKEN 环境变量后 `gh auth login`（设备流，注意 env var 会遮蔽 keyring 凭据）。
> 提交命令（正文须跳过本备注块，从 `## Summary` 起）：
> `tail -n +7 docs/performance-optimization/upstream-drogon-session-issue.md > /tmp/issue-body.md && gh issue create --repo drogonframework/drogon --title "drogon session retention: every cookie-less request creates a Session held for session_timeout (memory + throughput tax)" --body-file /tmp/issue-body.md`

## Summary

Upstream drogon behavior ([drogonframework/drogon#278](https://github.com/an-tao/drogon/issues/278), closed as by-design): when `enable_session: true`, `HttpAppFrameworkImpl::findSessionForRequest` creates a **new Session for every request that arrives without a session cookie** and holds it in `SessionManager`'s CacheMap until `session_timeout`. Eviction itself works correctly (verified: live heap 106 MB → 15 MB after the TTL window).

For an OAuth2 authorization server this is expensive: machine/API traffic (token, introspect, userinfo, discovery — all cookie-less) pays per request.

## Measured impact (2026-08-22, evidence: `docs/performance-optimization/backend-memory-retention-investigation.md`)

| Effect | Measurement |
|---|---|
| Retention | **~750 B per cookie-less request** until TTL (three 60s c128 storms: 744/755/759 B/req, production LTO build) |
| Steady-state formula | `API_QPS × session_timeout × 750 B` — 1k QPS @ 3600s ≈ 2.7 GB constant; 10k QPS @ 3600s ≈ 27 GB (OOM territory) |
| Throughput tax (all endpoints) | **~-54%** (production LTO build, 6-round interleaved OFF/ON, S1 discovery: 164.6k → 76.3k QPS) — session creation runs before routing on EVERY request |
| True framework ceiling | **~165k QPS** discovery (first-ever no-session measurement; all historical 87-104k numbers were session-limited) |
| Bench artifact | The 4-product comparison's "heaviest stack RSS 5,350 MiB" was this retention read after the S1 storm (backend 4.7 GB ≈ 5.4 M × 750 B + baseline) |

Session keys actually used by interactive flows: `userId/sub/auth_time/amr/mfa_*/webauthn_*` (8 keys, 4 interactive controllers only — zero use on machine endpoints).

## Current mitigation (delivered)

- Bench profile: `session_timeout: 30` (`config.bench.json`, e13041f) — caps retention at `QPS × 30 × 750 B` (~1.9 GB at 85k QPS); interactive flows unaffected (write→read gaps are milliseconds; S4 login/authcode validated).
- Deployment guidance with the validated formula: `docs/ops/deployment.md` §性能调优.

## Root-fix options (blocked on upstream)

1. **Upstream lazy/per-path session creation** — the 4 interactive controllers are the only consumers; machine endpoints never read session state, so skipping creation when the request carries no session cookie AND the route never touches `req->session()` is semantically safe. **The -54% throughput tax makes this a high-value upstream request** — discovery throughput would nearly double (76k → 165k).
2. Drogon upgrade if lazy sessions land (currently 1.9.13 via conan).
3. (Rejected for now) Migrating the 8-key login state to a signed cookie — viable but touches auth-critical flows; the -54% tax strengthens the case but requires careful security review.

## Production risk assessment

Retention is TTL-bounded (**no true leak** — eviction verified). Deployments with heavy API QPS should size `session_timeout` per the formula; interactive-only deployments are unaffected at 3600s.
