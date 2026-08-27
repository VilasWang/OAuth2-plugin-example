# fulla Competitor Performance Benchmark Comparison Design

> **Version**: v1.1 (2026-08-15 review revision: adds M0 prerequisite parameterization, S5 pool-size correction, pinned GC-jitter scenario, fulla same-session rerun, Zitadel JWT-profile authentication note, warmup methodology aligned with in-repo data)
> **Date**: 2026-08-15
> **Document type**: Technical design (Phase 0.5 implementation blueprint, **not code** — for implementation see the §7 milestones)
> **Upstream planning**: [Evolution Plan §3 Phase 0] (productization-evolution-plan: locally maintained archive), item P0 "self-hosted competitor comparison benchmark"
> **Prerequisites**: Benchmark infrastructure design (internal archive, moved to local maintenance along with the productization-evolution directory), M1–M4 delivered (S1–S6 self-test data committed under `benchmarks/results/`)
> **Verification target**: The competitor column (Keycloak/Ory) order-of-magnitude reference numbers in survey report §3.1 (internal archive, moved to local maintenance)

---

## 0. TL;DR

- **What**: Load-test Keycloak / Ory Hydra / Zitadel on the **same machine, with the same wrk ladder and the same PostgreSQL backend**, producing a like-for-like comparison table against fulla's committed self-test data (`benchmarks/results/SUMMARY.md`).
- **What gets compared**: The five single-step scenarios S1 discovery / S2 client_credentials / S3 introspect / S5 refresh_token / S6 userinfo, plus cold start, steady-state RSS, and the **GC-jitter long run** (5-minute P99 time series — fulla's core no-GC differentiation evidence).
- **What does not get compared**: S4 auth_code (each product's login/consent flows cannot be driven uniformly by wrk); Auth0 (SaaS, cannot be self-hosted); competitors' extreme tuned configurations (official recommended production configurations are used throughout).
- **Core principle**: Fairness over flattering numbers. Competitor communities will challenge the results, so the methodology must be beyond reproach — same hardware, same concurrency ladder, same backend, each product's officially recommended configuration, all scripts committed and reproducible.
- **Acceptance**: A four-product like-for-like comparison table (QPS / P99 / steady-state RSS / cold start / GC jitter) landed in `benchmarks/competitors/results/COMPARISON.md`; a third party can reproduce it one-command-style from the README.

---

## 1. Goals and Non-Goals

### 1.1 Goals

| # | Goal | Measured by |
|---|------|------|
| G1 | **Same-environment competitor data**: replace the survey report §3.1 competitor column ("from each product community's public benchmarks, not a same-environment comparison") with same-environment measurements | §6 acceptance ✅ COMPARISON.md |
| G2 | **Validate the differentiation narrative**: whether the C++ no-GC / low-memory / low-tail-latency claims are supported by same-environment data | GC-jitter long run + steady-state RSS comparison |
| G3 | **Reproducible**: a third party following `benchmarks/competitors/README.md` on an identically specced machine obtains data within &lt;15% deviation | Reproduction threshold (reusing the self-test infrastructure's AC1) |
| G4 | **Honest revision**: if fulla does not lead on some dimension, revise the claim ordering in survey report §3.1/§3.2 accordingly | Report update |

### 1.2 Non-Goals

| # | Non-Goal | Why / owner |
|---|--------|--------------|
| N1 | Hosted competitors such as Auth0 / Okta | SaaS cannot be self-hosted and the environment cannot be controlled; their public numbers are not reproducible. They remain only as order-of-magnitude references in the survey report |
| N2 | S4 auth_code scenario comparison | Each product's login page/redirect/consent interaction flows are completely different; wrk cannot drive them uniformly (see §4 D4) |
| N3 | Competitor extreme tuning | Officially recommended production configurations only. Extreme-tuning comparisons are an arms race with no credibility; official documentation links are attached alongside the results |
| N4 | Feature/protocol coverage comparison | That belongs to a product capability audit (iam-architecture-audit.md) and is unrelated to performance |
| N5 | Rerunning the fulla ladder data | fulla is still rerun in the same session (see the §5.1 v1.1 revision: gcjitter/RSS/cold start are mandatory + R7 eliminates drift); "reuse" only means reusing the same runner/methodology — the 2026-08-12 in-repo data is demoted to a historical baseline |

---

## 2. Background: Why Now

### 2.1 Phase 0 self-testing is done; the comparison is the last gap

Phase 0 (benchmark M1–M4) was delivered on 2026-08-12: six scenarios S1–S6, 40 JSON files, and a load-bearing validation report (`benchmarks/results/SUMMARY.md`). fulla's own numbers are now credible.

The survey report §3.1 competitor column (Keycloak ~10–20k QPS, Ory ~30–50k QPS) is annotated "public community benchmarks, not a same-environment comparison, order-of-magnitude reference only". **Without same-environment comparison data, "N× faster than Keycloak" must not appear in any external material.**

### 2.2 fulla self-test baseline (the comparison baseline, measured 2026-08-12)

| Scenario | Steady-state QPS | Steady-state P99 | Error rate |
|------|---------|---------|--------|
| S1 discovery | 86,332 | — (low concurrency &lt;1 ms) | 0.006% |
| S2 client_credentials | 8,915 | 8 ms | 0.000% |
| S3 introspect | 17,132 | 12 ms | 0.000% |
| S5 refresh_token | 1,982 | 26 ms | 0.000% |
| S6 userinfo | 16,674 | 12 ms | 0.000% |

Environment: WSL2 8 vCPU / 16GB / PG connection pool 25 / Redis 20 / wrk 4.1.0 / ladder 2→128.
See `benchmarks/results/SUMMARY.md` for details.

### 2.3 Existing reusable assets

| Asset | Reuse approach |
|------|---------|
| `run-scenario.sh` ladder runner (warmup→measure→JSON) | Competitor scenarios simply pass different `.lua` files; the runner gains **optional** parameters (`READY_PATH`/`RESULTS_DIR`/`WRK_LIB_DIR`/`--reissue` hook, see M0.1) — a single implementation, no second copy |
| `parse-wrk.py` (wrk text→schema v1 JSON) | Scenario-agnostic; only gains `--product/--product-version` pass-through parameters (M0.2) |
| `observe/docker-stats.sh` + `scrape-metrics.sh` | docker-stats gains a `CONTAINER_GLOB` parameter (M0.3) to sample competitor container RSS/CPU; scrape-metrics remains fulla-only (competitors have no `/metrics` endpoint; observation items are split) |
| `measure-cold-start.sh` pattern | One equivalent cold-start timing script per competitor |
| `lib/gen-tokens.py` idea | Competitor token pool generation (each product's introspect tokens must be issued by the product itself, see D5) |
| docker-compose base (PG15) | Competitor composes reuse the same PG version and connection pool configuration |

---

## 3. Key Constraints and Design Decisions

### D1 — Fairness: the three-same principle (same hardware / same tool / same backend)

**This is the credibility foundation of the entire plan.**

| Dimension | Unified value | Notes |
|------|--------|------|
| Hardware | The same machine (the WSL2 8 vCPU/16GB used by Phase 0, or a later dedicated bare-metal box) | The four products run sequentially, with `docker compose down -v` cleanup in between |
| OS/kernel | The same WSL2 Ubuntu | — |
| Load tool | wrk 4.1.0, the same ladder parameters (2→4→8→16→32→64→128), warmup 5 s / measure 10 s (aligned with the in-repo self-test methodology; Keycloak warmup 60 s, see the D2 exemption) | Reuses `run-scenario.sh`; no second runner is written |
| Backend storage | PostgreSQL 15 (the same image tag), connection pool aligned at 25 | A competitor's supported pool ceiling may be &lt;25; use `min(25, official ceiling)` and note it in the results |
| Network topology | localhost cross-container (wrk on the host) | Same as the self-test |
| Result format | schema v1 JSON (`parse-wrk.py`) | All four products' data are isomorphic, so `run-comparison.sh` can aggregate them |
| Ports | Fixed ports per stack (fulla 5555 / Keycloak 8080 / Hydra 4444+4445 / Zitadel 8080) | Serial execution avoids conflicts; each setup asserts up front that the ports are free |

### D2 — Competitor configuration = officially recommended production configuration (no tuning up, no tuning down)

**Rationale**: Extreme-tuning comparisons have no credibility; deliberately tuning down is academic fraud.

| Competitor | Configuration baseline | Official source |
|------|---------|---------|
| Keycloak | `start --optimized` + PostgreSQL, memory per the official 2GB limit recommendation | [Running Keycloak in a container](https://www.keycloak.org/server/containers) |
| Ory Hydra | Official docker-compose + PostgreSQL DSN | [Ory Hydra docs](https://www.ory.sh/docs/hydra/self-hosted/deploy-hydra) |
| Zitadel | Official compose (`setup mode` initialization + PostgreSQL) | [Set up ZITADEL with Docker Compose](https://zitadel.com/docs/self-hosting/deploy/compose) |

Each competitor's `setup.sh` header comment must link the official documentation; every deviation from the official defaults (e.g., connection pool alignment) is annotated individually with its rationale.

**JVM warmup exemption**: Keycloak's JIT/GC needs a longer warmup. Warmup is extended to 60 s for Keycloak (the other three keep 5 s, aligned with fulla's self-test methodology; the measurement duration is a uniform 10 s) and noted in the results — this is not favoritism, it is giving the JIT time to compile; otherwise what gets measured is "uncompiled interpreted execution".

### D3 — Scenario mapping: functional equivalence, not path equivalence

Endpoint paths differ per product; map to endpoints with the **same function** (§4 matrix). Key constraints:

- **S3 introspect, the Ory special case**: Hydra's introspect sits on the admin port (4445) rather than the public port, and in production deployments the admin port is usually not exposed. For fairness, all four products test introspect, but **COMPARISON.md annotates Ory's admin-port semantic difference**.
- **Authentication method**: client_credentials uses each product's standard client authentication (Basic or post, per its declaration).
- **Zitadel S2 special case (JWT profile)**: Zitadel's official M2M path is Service User + private_key_jwt (the token endpoint does **not** support client_credentials with Basic authentication). wrk Lua cannot sign JWTs, so the setup phase pre-signs a client_assertion pool with python (pyjwt + cryptography, already available in WSL), with exp covering the entire run window; if Zitadel enforces single-use jti, re-sign per step (equivalent to S5's --reissue mechanism). A COMPARISON.md appendix notes "the JWT profile is Zitadel's officially recommended machine authentication; it is functionally equivalent".

### D4 — Excluding S4 auth_code: not wrk-drivable

fulla's S4 is a two-step form POST `login → token` (headless-drivable). But:
- **Keycloak** auth_code requires its login page's HTML form + JS
- **Ory Hydra** requires an external login/consent app (Hydra itself has no login UI)
- **Zitadel** has its own login session flow

Driving all of these uniformly would require a real browser (Playwright), and what would be measured is "login page rendering", not "token issuance". **S4 is excluded from the first iteration, with the limitation noted in COMPARISON.md**; if needed later, approximate it with "pre-issued code + single-step token exchange" (each product's pre-issuance method differs and the complexity is high — revisit in Phase 0.6).

### D5 — Competitor token pools must be issued by the products themselves (no direct SQL inserts)

fulla's self-tests pre-seed tokens for S3/S6 via SQL (`gen-tokens.py`). **Not possible for competitors**:
- Keycloak's access tokens are signed JWTs with private hash/key formats
- Hydra/Zitadel likewise

**Approach**: Each competitor's `setup.sh` issues tokens in bulk through **its own token endpoint**, writes the live tokens into a token-pool file, and the Lua scenario scripts reuse the same token-pool logic (thread slicing, reusing the `s3-introspect.lua` pattern).

**Pool size (v1.1 revision — the two pool types have different bases)**:
- **S3/S6 pool (reusable)**: Tokens are only read-validated, never consumed; N=2000 suffices; bulk issuance itself takes ~1–2 minutes (parallel xargs -P8 is faster).
- **S5 pool (single-issued, single-consumed)**: Every refresh token is invalidated after a single use; the pool must be ≥ that step's QPS × measurement duration × a 1.3 margin. fulla's self-test measured basis: 20,000 pool ÷ 10 s ≈ 1,982 QPS (the pool exactly covers the measurement window). Competitors must **re-issue the pool** before every step (`run-scenario.sh` gains a `--reissue "<cmd>"` hook, equivalent to the self-test's SQL `--reseed` but going through each product's API).
- **RTs cannot come from client_credentials**: RFC 6749 §4.4.3 forbids issuing refresh tokens under that grant. Competitor RT pools must be obtained through user-context flows — Keycloak via ROPC (direct access grants); Hydra via the accept flow (see M2); Zitadel via the Session API/auth_code (see M2).

### D6 — GC jitter = long-run P99 time series (fulla's core differentiation evidence)

A single 30 s run cannot reveal GC cycles. A dedicated **long-run test** is designed:

```
Per product: c=32 fixed, sustained for 5 minutes, scenario pinned to S6 userinfo
(v1.1 revision: S5 is unusable — the pool would be exhausted; S2 has write
amplification — fulla persists one token row to the DB per request, so at 5
minutes ~8k QPS ≈ 2.4M rows, an asymmetric workload across the four products;
S6 is a read path, its token pool is reusable, and all four products are
functionally equivalent — the fairest carrier. If S6 is marked N/A for Hydra,
fall back to S2 and note it in the results.)
Sampling: record each 10 s window's P99 once (wrk has no native segmentation
→ approximated by 30 serial 10 s segments)
Output: a P99-over-time curve (JSON array)
Expected: Keycloak shows periodic P99 spikes (GC STW); Ory shows small Go GC
spikes; fulla stays flat
```

Implementation: `benchmarks/competitors/run-gc-jitter.sh` — loops wrk `-d10s` 30 times in series, parsing each segment's P99 into an array. **This is the single most compelling chart for the external narrative** (visual evidence of "zero GC jitter").

### D7 — Unified memory basis: full-stack container RSS + annotated logical layer

Phase 0 load-bearing validation found fulla's container RSS is ~2.4GB (including the Drogon connection pool/shared-library COW), which does not match the "50–120MB" claimed figure. The comparison plan unifies the basis:

| Basis | Collection | Use |
|------|------|------|
| Full-stack container RSS | steady-state `docker stats` sampling (reusing the observe scripts) | **Primary basis for the comparison table** — comparable across all four products on the same basis |
| Process PSS (optional) | `smem` / `/proc/*/smaps_rollup` | Supplementary basis eliminating double counting of COW shared pages |

COMPARISON.md explicitly states "full-stack container RSS, including each product's own runtime + connection pool" to avoid basis disputes.

---

## 4. Comparison Scenario Matrix

### 4.1 Scenario × competitor endpoint mapping (verified against official documentation)

| Scenario | Function | fulla | Keycloak | Ory Hydra | Zitadel |
|------|------|-----------|----------|-----------|---------|
| **S1** discovery | OIDC discovery document | `GET /.well-known/openid-configuration` | `GET /realms/{r}/.well-known/openid-configuration` | `GET /.well-known/openid-configuration` (public :4444) | `GET /.well-known/openid-configuration` |
| **S2** client_credentials | machine-to-machine token | `POST /oauth2/token` | `POST /realms/{r}/protocol/openid-connect/token` | `POST /oauth2/token` (public :4444) | `POST /oauth/v2/token` |
| **S3** introspect | token introspection | `POST /oauth2/introspect` | `POST /realms/{r}/protocol/openid-connect/token/introspect` | `POST /admin/oauth2/introspect` (admin :4445) ⚠ | `POST /oauth/v2/introspect` |
| **S5** refresh_token | token refresh | `POST /oauth2/token` (refresh) | `POST /realms/{r}/protocol/openid-connect/token` (refresh) | `POST /oauth2/token` (refresh) | `POST /oauth/v2/token` (refresh) |
| **S6** userinfo | user information | `GET /oauth2/userinfo` | `GET /realms/{r}/protocol/openid-connect/userinfo` | `GET /userinfo` (public :4444) | `GET /oidc/v1/userinfo` |
| ~~S4~~ auth_code | user login | ~~`login→token`~~ | ~~login page not headless-drivable~~ | ~~external consent app required~~ | ~~login session flow~~ |

> ⚠ Ory's admin-port semantic difference for introspect is noted in the results. The Keycloak realm name, Hydra's public/admin dual ports, and Zitadel's instance domain are all fixed as constants in their respective setup scripts.

Endpoint sources:
- Keycloak: [OpenID Connect endpoints](https://www.keycloak.org/docs/latest/securing_apps/) (`/realms/{realm}/protocol/openid-connect/*`)
- Ory Hydra: [API docs](https://www.ory.sh/docs/hydra/reference/api) (public :4444 / admin :4445)
- Zitadel: [OpenID Connect Endpoints](https://zitadel.com/docs/apis/openidoauth/endpoints)

### 4.2 Metric matrix

| Metric | Collection method | Comparison-table column |
|------|---------|---------|
| Steady-state QPS (highest step with err&lt;0.01%) | wrk ladder + parse-wrk.py | ✅ |
| P50 / P95 / P99 (steady-state step) | wrk `--latency` | ✅ |
| Error rate | wrk non-2xx | ✅ (threshold column) |
| Cold start (→health 200) | equivalent timing script per product | ✅ |
| Steady-state container RSS | docker stats sampling | ✅ |
| GC jitter (5-min P99 curve) | run-gc-jitter.sh | ✅ (dedicated section) |
| Driver CPU | existing run-scenario.sh sampling | ✅ (credibility annotation) |

---

## 5. Test Strategy

### 5.1 Execution order (single machine, serial, to prevent mutual interference)

```
1. fulla    — rerun in the same session (v1.1 revision: gcjitter/RSS/cold start are mandatory
                  items for AC1/AC2; Phase 0 never sampled gcjitter; and R7 requires all four
                  products to run back-to-back in the same session to eliminate cross-day
                  environment drift. The 2026-08-12 in-repo data is kept as a historical
                  baseline; fresh same-session data is used for the comparison)
2. Keycloak     — setup → ladder → long run → cold start → teardown
3. Ory Hydra    — same as above
4. Zitadel      — same as above
Between products: docker compose down -v + assert no leftover containers/volumes/networks
```

`run-comparison.sh --fresh` runs everything serially; `--only keycloak` supports re-running a single product.

### 5.2 Competitor setup conventions (one setup.sh per product)

Every `setup.sh` must:
1. Start the competitor + PostgreSQL (connection pool aligned, see D1)
2. Wait for health (each product's ready probe: Keycloak `/realms/master`, Hydra `/health/ready`, Zitadel `/debug/healthz`)
3. Initialize configuration (realm/client/user — via each product's CLI or admin API)
4. **Bulk-issue the token pool** (D5: N client_credentials requests → `access_tokens.txt`)
5. Warmup validation (one token request to confirm the pipeline works)

### 5.3 Long-run GC jitter (D6)

Parameters: `c = the concurrency step at each product's steady-state knee`, 30 × 10 s segments (5 minutes total), no gaps between segments.
Output: `results/<date>-<sha>-<product>-gcjitter.json` (P99 array + segment timestamps).

### 5.4 Cold start

Timed independently per product: `docker compose up -d <idp>` → poll the ready probe for 200.
Recorded in two modes (with/without DB warm-up), aligned with the self-test `measure-cold-start.sh`.

### 5.5 Environment metadata

The `env` block of the result JSONs follows schema v1, gaining `product` / `product_version` fields (parse-wrk.py gains one pass-through parameter); the COMPARISON.md table header lists the versions.

---

## 6. Acceptance Criteria (checkable)

> ✅ All passed 2026-08-17 (M0–M3 delivered; the four products measured same-session on 2026-08-17; COMPARISON.md generated by gen-comparison.py).
> 🔄 Full rerun refresh 2026-08-21 (same infrastructure, fulla on an optimized baseline step — wave-1/2 + LTO, see the G1 delivery note): **all five scenarios ahead** (previously behind on S5/S6 — S5's measurement-budget artifact has had its basis fixed, and S6 overtook via the wave-2 user/role cache); the GC section adds cross-product environmental-noise cross-validation (identical spikes on all four). Survey report §3.1 now cites the new table.

| # | Acceptance item | Measured by | Status |
|---|--------|------|------|
| AC1 | **Four-product like-for-like comparison table**: S1/S2/S3/S5/S6 × \{QPS, P99, RSS, cold start\} committed to `benchmarks/competitors/results/COMPARISON.md`, each row carrying the product version | COMPARISON.md | ✅ |
| AC2 | **GC jitter curves**: four products' 5-minute P99 time-series JSON + comparison section (is fulla flat; does Keycloak show periodic spikes) | gcjitter JSON + section | ✅ (conclusion opposite to expectation, see the G4 note: the GC languages are all flat and fulla shows environment-level second-scale spikes — honestly revised in the report and the research report) |
| AC3 | **Reproducible**: `run-comparison.sh` runs all four products serially with one command; the README covers environment requirements and reproduction steps | Reproduction guide | ✅ (`benchmarks/competitors/README.md`) |
| AC4 | **Fairness statement**: each competitor's configuration source (official documentation links), every deviation from the defaults, and the warmup difference (Keycloak 60 s) are all explicitly annotated | COMPARISON.md appendix + setup.sh comments | ✅ |
| AC5 | **Honest revision**: survey report §3.1 competitor column updated to "same-environment measurements"; §3.2 claims converge if falsified | research.md update | ✅ (the S5/S6 and GC-jitter claims converged per the measurements, see the §3.1/3.2 revisions) |
| AC6 | **S4 exclusion statement**: COMPARISON.md notes the method limitation for the auth_code scenario | Limitations section | ✅ (appendix B.1) |

### Implementation errata (v1.2, revised during the 2026-08-17 implementation; v1.3 adds two items on 2026-08-18)

Decisions made during implementation that deviate from this design v1.1 — all of them fairness/feasibility corrections:

1. **Zitadel version v2.71.19 → v4.17.1**: v2.71 was two major versions behind (the current stable line is v4, the same generation as Keycloak 26 / Hydra 26), and v4 is an eventstore/projection performance rewrite. Testing the old version would distort and disparage Zitadel — indefensible. The first-attempted `v1.80.0-v2.9-amd64` tag turned out to be a v1-era CockroachDB-only image; discarded.
2. **Zitadel S2 authentication path corrected**: Design D3's original wording "client_assertion (JWT profile)" did not work in v2.71/v4 testing — Zitadel's token endpoint handles client_credentials for machine users via Basic-secret only (password hashing per request); the official M2M path is the **RFC 7523 jwt-bearer grant** (`grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=…`). The S2/S3 equivalence annotations were updated accordingly.
3. **Zitadel S3 authentication switched to a private key**: official advice in #6220 — secret authentication password-hashes on every request (a CPU bottleneck); switched to an OIDC app + private_key_jwt. Measured comparison: the Basic path could not pass S3's error gate; the private-key path reached 2.9k QPS with zero errors.
4. **Projection settling gate**: Starting the load immediately after minting ~2000 tokens lets Zitadel's CQRS projections fall behind, causing an S1 500-storm (up to 99.99% errors at c=16). run-all now has a discovery smoke gate up front (only starts when clean), eliminating the artifact.
5. **S5 Zitadel = N/A** (DG-2 early ruling): machine users have no refresh tokens (RFC 6749 §4.4.3), the password grant has been removed, and Session API→auth_code is a user-interaction flow (the same exclusion rationale as D4). Noted in the limitations section.
6. **docker-stats.sh pipe-glob regression fixed**: under bash 5.2, `case $x in $GLOB)` (with `|` inside GLOB) no longer expands into multiple branches — fulla-side RSS sampling had been silently empty since the M0 parameterization; changed to split-and-match individually. This regression also explains the missing fulla RSS data after v1.1.
7. **fulla S2 scope follows #43**: the seed drops legacy `read/write`; the bench validation and the s2 lua use `tokens:read` (same code, same test — not a basis change).
8. **(v1.3) Four products upgraded PG 15 → 17 in sync** (2026-08-18, f789bda): The design baseline was "the same `postgres:15-alpine` tag as the self-test". Deviation motive: D1 same-environment fairness — keeping all four products on the same PG major version; the fulla server's libpq was already 17.x, removing the client 17/server 15 skew. fulla's own 15 vs 17 A/B is within the noise band (no self-interested throughput motive). deploy/'s compose and Helm moved to 17 in sync (the existing-volume upgrade runbook is at `docs/operate/postgresql-major-upgrade.md`; the CHANGELOG is marked BREAKING).
9. **(v1.3) fulla Redis pool 25 → 64** (2026-08-18, quick win 8838ac6): The design's §5.2 connection-pool basis was aligned at pool=25; during implementation, cache-on required pool ≥ the expected concurrency (with pool 20, S6 hit all-connection timeouts, -18%), so the bench overlay was raised to 64. Competitors run at their own official default pool basis (noted in the COMPARISON.md fairness appendix).

---

## 7. Implementation Plan (4 milestones, each step with acceptance criteria)

> v1.1: M0 added at review — parameterization of the shared infrastructure (backward compatible; behavior is unchanged when the new env vars are absent).

### M0 — Shared infrastructure parameterization (prerequisite, ~0.5 day)

| # | Step | Content | Acceptance criteria |
|---|------|------|---------|
| M0.1 | `run-scenario.sh` parameterization | (a) `READY_PATH` env (default `/health/ready`) — the health gate's probe can be swapped for a competitor probe; (b) `RESULTS_DIR` env (default `benchmarks/results`); (c) the `WRK_LIB_DIR` export becomes `${WRK_LIB_DIR:-$BENCH_DIR/lib}` (competitor luas point at their own lib); (d) `BENCH_PRODUCT`/`BENCH_PRODUCT_VERSION` env passed through to parse-wrk.py; (e) a generic `--reissue "<cmd>"` hook: executed once before each step's warmup and once before measured (competitor S5 re-issues the token pool via API, replacing fulla's SQL `--reseed`); (f) `--observe` split into `--observe-stats` (docker-stats only) and `--observe-metrics` (scrape-metrics only; unused for competitors without `/metrics`) | **AC-M0.1a** Running fulla S1 as a single step (c=2, -d10s) without any new env behaves identically to before the change (JSON schema, defaults, and output paths all unchanged); **AC-M0.1b** With `READY_PATH=<any 200 path> RESULTS_DIR=<tmp> BENCH_PRODUCT=x`, the health gate uses the new path, the JSON lands in the new directory, and env.product=x; **AC-M0.1c** `--reissue "touch $TMP/marker"` smoke: two markers per step (pre-warmup + pre-measured) |
| M0.2 | `parse-wrk.py` pass-through | `--product`/`--product-version` CLI args → new `product`/`product_version` fields in the env block (defaults `fulla`/empty) | **AC-M0.2** A wrk sample text parsed via pipe yields a JSON containing both fields with correct values; without the args, the defaults do not break the existing aggregation |
| M0.3 | `docker-stats.sh` container-filter parameterization | `CONTAINER_GLOB` env (default keeps the `*fulla-backend*|*fulla-postgres*|*fulla-redis*` semantics) | **AC-M0.3** Sampling with `CONTAINER_GLOB='*keycloak*'` outputs keycloak container rows and no fulla rows |
| M0.4 | Schema documentation sync | the `result-schema.md` env block gains `product`/`product_version` field descriptions | The documentation's field table matches the implementation (cross-checked) |

### M1 — Keycloak comparison (the heaviest; hardest nut first, ~2 days)

Configuration baseline (D2): `quay.io/keycloak/keycloak:<pinned version>` + `start --optimized` + `--memory 2G` (the official container recommendation), PostgreSQL on the **same image tag** as the self-test — `postgres:15-alpine`, port 8080.

| # | Step | Content | Acceptance criteria |
|---|------|------|---------|
| M1.1 | `docker-compose.yml` | keycloak + postgres; healthcheck hits `/realms/master`; volumes/networks explicitly prefixed `kc-bench-` | **AC-M1.1a** After `up -d`, the probe returns 200; **AC-M1.1b** After `down -v`, `docker ps -a`/`docker volume ls`/`docker network ls` show no kc-bench leftovers |
| M1.2 | `setup.sh` | Wait for health → `kcadm.sh` creates realm `bench`, client `bench-svc` (confidential + service account + introspection permissions), user `bench-user` (direct grants) → **calibration run** (c=8 single-step S2/S5/S6 to estimate QPS) → generate the RT pool per `pool = QPS×10s×1.3` (bulk-issued via ROPC, parallel with xargs -P8) + AT pool ≥2000 (S3/S6 reuse) → warmup: verify one token request | **AC-M1.2a** `set -euo pipefail`; any kcadm/curl failure exits non-zero; **AC-M1.2b** Final self-check: both pool files have ≥ the expected line counts and a single client_credentials returns 200; **AC-M1.2c** Idempotent: after `down -v`, rerunning setup succeeds |
| M1.3 | `scenarios/` (5 luas) | s1/s2/s3/s5/s6 rewritten per the §4.1 endpoints; S2 uses Basic (bench-svc); S3 Basic + AT pool (thread slicing, reusing the s3 pattern); S5 RT one-shot pool; S6 Bearer AT pool | **AC-M1.3** Each scenario `wrk c=2 -d5s` smoke: non-2xx=0, socket errors=0 (S5 tolerates pool-tail nil-closed connections, but there must be no invalid_grant) |
| M1.4 | Ladder data | `WARMUP_S=60 DURATION_S=10`, ladder 2→128 × 5 scenarios → 35 JSONs; the S2 step attaches `--observe-stats` for RSS | **AC-M1.4a** All 35 JSONs carry `product=keycloak` + the version; **AC-M1.4b** Per step, driver CPU &lt;80% or the JSON is flagged `limited=true`; **AC-M1.4c** The RSS tsv is written and contains keycloak+postgres rows |
| M1.5 | GC jitter | `run-gc-jitter.sh`: S6 c=32, 30×10 s segments (D6) | **AC-M1.5** The JSON contains 30 P99 data points + segment start timestamps; no failed segments |
| M1.6 | Cold start | Two modes: A = full initialization on a fresh volume (including realm/client creation); B = pre-initialized volume, restarting only the keycloak container | **AC-M1.6** 2 JSONs (modes A/B), containing seconds and peak RSS |
| M1.7 | `teardown.sh` | down -v + leftover assertion | Same as AC-M1.1b |
| M1.8 | First two-way comparison | fulla same-session rerun (§5.1) + Keycloak data → draft comparison table | **AC-M1.8** 5 scenarios × `QPS / P50/P95/P99 / RSS / cold start` rows complete, version columns complete |

### M2 — Ory Hydra + Zitadel (~3 days)

**Hydra**: No built-in user system. User tokens for S5/S6 are driven headless via the official mock pattern: `GET /oauth2/auth` (login redirect) → admin API `POST /admin/oauth2/auth/requests/login/accept` + `consent/accept` → code → token; drivable with pure curl (no browser needed).
**Zitadel**: S2 uses the JWT-profile pre-signed assertion pool (D3 special case); S3 uses an API client + Basic; S5/S6 prefer the v2 Session API to create a password session → auth_code exchanged for user tokens.

| # | Step | Acceptance criteria |
|---|------|---------|
| M2.1 | Hydra compose (hydra v2 + PG, public 4444 / admin 4445) + setup (client create + accept-flow-driven pool issuance)| Same pattern as M1.1/M1.2: `/health/ready` probe 200; pool-file line-count self-check; any curl/jq failure exits non-zero |
| M2.2 | Hydra scenarios + ladder + gcjitter + cold start | Same acceptance as M1.3–M1.6; the S3 JSON/results carry the admin-port annotation |
| M2.3 | Zitadel compose (`setup` mode initialization + `start` mode run) + setup (machine-user JSON key, API client, human user) | Same as M1.1/M1.2; the setup's two phases (init/start) are individually re-entrant |
| M2.4 | Zitadel scenarios + ladder + gcjitter + cold start | Same as M1.3–M1.6; the S2 results note the JWT-profile authentication equivalence |

**Decision gates (within M2)**:
- **DG-1**: If Hydra's accept flow cannot be curl-driven (e.g., mandatory JS), S5/S6 are marked N/A. Criterion: the setup can reliably obtain a user token carrying the `openid` scope.
- **DG-2**: If Zitadel's Session API→auth_code cannot be driven within 2 working days, S5/S6 are marked N/A (S1/S2/S3 as the fallback), noted in the limitations section.

### M3 — Aggregation + honest revision (~1 day)

| # | Step | Acceptance criteria |
|---|------|---------|
| M3.1 | `gen-comparison.py` aggregator | Reads all four products' JSONs and fully auto-generates COMPARISON.md: main table (5 scenarios × QPS/P50/P95/P99/RSS/cold start × version columns), GC-jitter section, fairness appendix (configuration sources + deviation items + warmup differences), limitations section (S4 exclusion, Ory admin-port, Zitadel JWT-profile, WSL2 statement). **AC-M3.1**: no hand-entered numbers; a missing product/scenario renders an explicit N/A, never a missing row |
| M3.2 | `run-comparison.sh` orchestrator | `--fresh` runs everything serially (cleanup + leftover assertion between products) + `--only <product>` re-runs + auto-invokes the aggregator at the end. **AC-M3.2**: one command from an empty environment to COMPARISON.md |
| M3.3 | research.md §3.1/§3.2 revision | The competitor column becomes "same-environment measurements"; the claims converge per the measurements. **AC-M3.3**: every number is traceable to an in-repo JSON |
| M3.4 | Documentation wrap-up | benchmarks/README.md gains the competitors guide; this document's §6 acceptance checked off | AC1–AC6 all checked |

---

## 8. Directory Layout Design (design only)

```
benchmarks/competitors/
├── README.md                      # Reproduction guide (environment/order/limitations)
├── run-comparison.sh              # Runs everything serially (or --only <product>)
├── run-gc-jitter.sh               # 5-minute P99 time series (D6)
├── keycloak/
│   ├── docker-compose.yml         # Keycloak + PG (aligned per D1)
│   ├── setup.sh                   # start --optimized + kcadm init + token pool
│   ├── teardown.sh
│   ├── lib/generated/             # setup outputs: token pool files (WRK_LIB_DIR points here)
│   └── scenarios/                 # s1/s2/s3/s5/s6.lua (endpoints per §4.1)
├── ory/
│   ├── docker-compose.yml         # Hydra(+Kratos if needed) + PG
│   ├── setup.sh                   # hydra client create + token pool
│   ├── teardown.sh
│   └── scenarios/
├── zitadel/
│   ├── docker-compose.yml
│   ├── setup.sh                   # setup mode init + token pool
│   ├── teardown.sh
│   └── scenarios/
└── results/
    ├── <date>-<sha>-<product>-<scenario>-c<conn>.json   # schema v1
    ├── <date>-<sha>-<product>-gcjitter.json
    └── COMPARISON.md              # Aggregated comparison table (generated by gen-comparison.py, M3)

# The aggregator lives in the shared reporting/ (same level as parse-wrk.py):
benchmarks/reporting/gen-comparison.py
```

**Reuse, don't duplicate**: `run-scenario.sh` / `parse-wrk.py` / `observe/` directly reference the existing implementations in `benchmarks/fulla/` and `benchmarks/reporting/` (via path parameters or environment variables); no second runner copy is made for competitors.

---

## 9. Risks and Mitigations

| Risk | Level | Impact | Mitigation |
|------|------|------|------|
| **Competitor communities challenge the configuration as unfair** | High | External data overturned, reputation damaged | D2 official recommended configuration + AC4 full annotation of deviation items; competitor communities can be invited to review the setup scripts before publication |
| **Keycloak JIT/GC under-warmed, low numbers called unfair** | High | Keycloak's numbers artificially low | Warmup extended to 60 s + the long run discards the first minute of segments |
| **Hydra has no built-in users, S6 untestable** | Medium | Scenario coverage gap | Equip Hydra with a minimal login/consent mock app (the official brownfield pattern); if the complexity exceeds expectations, S6 is marked N/A for Hydra |
| **S5 competitor pool undersized / reissue too slow** | Medium | S5 data distorted or the setup times out | The calibration run sizes the pool (QPS×10s×1.3); parallel issuance via xargs -P8; --reissue re-issues per step (D5 v1.1) |
| **Zitadel token endpoint lacks Basic client_credentials support** | Medium | S2 cannot be measured on the unified Basic basis | JWT-profile pre-signed assertion pool (the officially recommended path, signed with pyjwt); COMPARISON notes the authentication equivalence (D3 v1.1) |
| **Zitadel Session API→auth_code driving fails** | Medium | S5/S6 gap | Decision gate DG-2: mark N/A if not working within 2 working days; S1/S2/S3 as the fallback |
| **Slow competitor token pool issuance (2000 API calls)** | Low | Long setup time | Parallel issuance (xargs -P8); or reduce the pool to 500 (the S3/S6 pool is reusable — that is enough) |
| **fulla not leading on some dimension** | Medium | Selling-point narrative damaged | That is precisely the infrastructure's value — converge honestly onto the leading dimensions (the established contingency in Evolution Plan §2 principle 1) |
| **Single-machine serial runs, environment drift (OS updates/temperature)** | Medium | The four products' data come from different batches and are not comparable | Run back-to-back within one session; timestamp every product's results; take the median across reruns |
| **wrk cannot saturate the Go/Java services (driver-limited)** | Low | The numbers are lower bounds | Keep the AC4 driver CPU gate; annotate above 80% |

---

## Appendix A: Relationship to Upstream Documents

| Upstream | Relationship |
|------|------|
| Benchmark infrastructure design (internal archive, moved to local maintenance with the productization-evolution directory) | This document expands its N1 (Phase 0.5 competitor comparison); it reuses that design's runner/parser/schema/observe |
| [Evolution Plan] (productization-evolution-plan: locally maintained archive) §3 Phase 0 P0 item 2 | This document is the implementation design for that work item |
| Survey report (internal archive) §3.1 | The **replacement data source** for the competitor column; M3 revises it per the facts |
| `benchmarks/results/SUMMARY.md` | fulla-side baseline (same-environment self-test, 2026-08-12) |

## Appendix B: Decision Record

| Decision | Choice | Alternatives and rejection rationale |
|------|------|---------------|
| Comparison targets | Keycloak / Ory / Zitadel | Auth0 (SaaS, not self-hostable) rejected |
| Scenario scope | S1/S2/S3/S5/S6 | S4 (login flow not headless-drivable) excluded, see D4 |
| Configuration baseline | Officially recommended production configuration | Extreme tuning (an arms race with no credibility) and default dev configuration (unfair) both rejected |
| Token pool | Self-issued via each product's API | Direct SQL insertion (private signature formats) rejected, see D5 |
| Memory basis | Full-stack container RSS (primary) + PSS (supplementary) | A single basis necessarily disadvantages one side; both bases are presented side by side with annotation |
| Warmup/measure basis (v1.1) | 5 s/10 s, Keycloak warmup 60 s | Aligned with the in-repo self-test data; the original text's "10 s for the other four" did not match the actual data (5 s/10 s) and was corrected |
| GC-jitter carrier scenario (v1.1) | S6 userinfo, c=32 fixed | S5 (pool exhaustion) and S2 (fulla writes to the DB per request; asymmetric workload) rejected, see D6 |
| fulla data basis (v1.1) | Same-session rerun | "Reusing the old data" contradicted AC2 (gcjitter mandatory) + R7 (same session eliminates drift) and was corrected |
| S5 competitor pool (v1.1) | API reissue per step (--reissue), pool = QPS×10s×1.3 | A fixed 2000 pool (single-use, single-consumption — would be exhausted) rejected; RTs not taken from client_credentials (forbidden by RFC 6749 §4.4.3) |
