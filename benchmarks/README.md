# AuthForge HTTP performance benchmarks

This directory holds the **reproducible, out-of-process** HTTP benchmarks for
AuthForge — the Phase 0 "credibility baseline" of the
[productization evolution plan](../docs/productization-evolution/productization-evolution-plan.md).
Its purpose is to turn the load-bearing performance claims in the
[research report §3.1](../docs/productization-evolution/productization-research.md)
(QPS / latency / memory / cold-start) from **assertions into measurements**.

> Full design (scenario matrix, methodology, acceptance criteria): see
> [benchmark-facility-design.md](../docs/productization-evolution/in-progress/benchmark-facility-design.md).
> This README is the **how-to-run** companion; the design doc is the **why**.

## Scope of M1 (current)

| | |
|---|---|
| **Target** | postgres + redis full stack via `deploy/docker/docker-compose.yml` (backend on `:5555`), **not** memory mode |
| **Scenarios** | **S1 discovery** + **S2 client_credentials** — the two cheapest, most stable paths that exercise the full pipeline (target boot → wrk → JSON result) |
| **Out of scope (M2–M4)** | S3 introspect, S4 auth_code, S5 refresh_token, S6 userinfo, competitor comparison (Phase 0.5), the load-bearing-assumption verification report |

S1/S2 are deliberately the simplest: S1 (discovery/jwks) is stateless and
exercises the pure Drogon framework ceiling; S2 (client_credentials) is a
single-step token issuance with no user/session state. Together they validate
the harness end-to-end before the more complex multi-step scenarios land.

## Prerequisites

- **Docker** + **docker compose** (to bring up the target stack)
- **wrk** (the load driver; C, event-driven, low client overhead — TechEmpower
  same):
  - Debian/Ubuntu: `sudo apt-get install -y wrk`
  - macOS: `brew install wrk`
  - Alpine: `apk add --no-cache wrk`
- **python3** (for the result parser; no extra packages needed)
- **curl** (for health/seed checks in `setup.sh`)

No local PostgreSQL/Redis is required — both run inside the compose stack.

## Quick start

```bash
# 1. Boot the full stack, gate on /health/ready, validate seed data.
bash benchmarks/authforge/setup.sh

# 2. Run a scenario staircase (default: 2→4→8→16→32→64→128→256 connections).
bash benchmarks/authforge/run-scenario.sh scenarios/s2-client-credentials.lua
bash benchmarks/authforge/run-scenario.sh scenarios/s1-discovery.lua

# 3. (optional) Run just a few low levels for a quick smoke check:
bash benchmarks/authforge/run-scenario.sh scenarios/s2-client-credentials.lua 2 4 8

# 4. Tear down + reset volumes (deterministic for the next run).
bash benchmarks/authforge/teardown.sh
```

Each level writes one JSON result to `benchmarks/results/` named
`<YYYYMMDD>-<git-sha>-<scenario>-c<conn>.json` (see
[`authforge/lib/result-schema.md`](authforge/lib/result-schema.md) for the
schema). The run prints a one-line summary per level, e.g.:

```
=== s2-client-credentials  c=64  t=4 ===
  -> 20260808-abc1234-s2-client-credentials-c64.json: QPS=50,780  P99=0.003s  err=0.0008%  driver_cpu=42.5%
```

## Reading the results

- **QPS** (`qps`): primary throughput. Find the **knee** — the level beyond
  which QPS stops growing (~5% over two consecutive levels) while P99 climbs.
- **P99** (`latency_us.p99`): tail latency. The research report claims <2ms;
  M4 will verify this honestly.
- **error_rate**: must be `< 0.01%` for a level to count as the **steady-state
  capacity** (what gets reported externally), per AC5.
- **driver.limited**: `true` when the wrk process hit ≥80% CPU — the number is
  then a **lower bound**, not a ceiling (AC4). If this fires, use a stronger
  driver machine or multiple wrk instances.

## Configuration knobs (env vars)

| Variable | Default | Effect |
|---|---|---|
| `TARGET_URL` | `http://127.0.0.1:5555` | where the backend is reachable |
| `WARMUP_S` | `10` | per-level warmup seconds (discarded) |
| `DURATION_S` | `30` | per-level measured-run seconds |
| `DRIVER_CPU_GATE` | `80` | wrk CPU% at/above which a level is marked `limited` |
| `KEEP_VOLUME` | `0` | `=1` makes setup/teardown preserve the DB volume |
| `BENCH_TARGET_SPEC` / `BENCH_DRIVER_SPEC` | empty | recorded into each result's `env` block (e.g. `"4vCPU/8GB"`) |

## How the seed data works

The docker-compose stack does **not** auto-seed the database. Schema migrations
run on the app's startup (`OAUTH2_AUTO_MIGRATE=true` → `MigrationRunner`), but
the app never applies seed SQL, and the postgres entrypoint's `initdb.d/seed/`
subdirectory mount is a documented no-op (postgres does not recurse into
subdirs). So `setup.sh` **explicitly applies** every `apps/server/seed/*.sql`
file via `docker exec ... psql` after the stack boots, retrying until the
migration-created tables exist (the migration runs on a detached startup thread).

The seed files are:

- `dev_backend_client.sql` — the `backend-svc` CONFIDENTIAL client (S2 needs this)
- `dev_vue_client.sql` — the `vue-client` PUBLIC client (M2 S4/S5/S6)
- `dev_admin_user.sql` — `admin/admin` (**smoke only; do not load-test** —
  progressive lockout at 5/10/15/20 failed logins, `AuthService.cc:149-157`)
- `bench_users.sql` — **512 dedicated `bench_user_NNNN` users** (password
  `admin`, same legacy SHA256+salt). M1 scenarios do not consume these; they
  are seeded now so M2's auth_code scenario can bind each virtual user to a
  distinct account (avoiding lockout). Their first login triggers a PBKDF2
  rehash; `setup.sh` exposes a `warmup_bench_users` function M2 will invoke
  before timed runs so that CPU cost lands in warmup, not measured throughput.

All seed files are idempotent (`ON CONFLICT DO NOTHING`), so re-applying on an
existing volume is safe.

**Determinism**: `teardown.sh` removes volumes by default (`docker compose
down -v`), so every `setup.sh` starts from the same schema + seed. Use
`KEEP_VOLUME=1` to re-run against an existing stack.

## Known limitations

- **CI shared runners give non-credible absolute numbers** — neighbors steal
  CPU, so absolute QPS/latency from a CI run are meaningless. CI regression
  gates (a future `benchmarks-regression.yml`, design §9) will only compare
  **relative** deltas against a master baseline. For externally-cited absolute
  numbers, use a **dedicated benchmark machine** and record its spec in the
  result `env`.
- **Driver must not be the bottleneck.** A single wrk instance saturating its
  CPU cannot drive a fast C++ server to its limit. If `driver.limited` fires,
  the reported QPS is a lower bound — switch to a beefier driver or aggregate
  multiple wrk instances.
- **S1/S2 only in M1.** The full scenario matrix (introspect, auth_code,
  refresh_token, userinfo) lands in M2–M3; the load-bearing-assumption
  verification report lands in M4.

## Layout

```
benchmarks/
├── README.md                  ← you are here
├── authforge/
│   ├── setup.sh               # boot stack + health gate + seed validation
│   ├── teardown.sh            # stop + (default) remove volumes
│   ├── run-scenario.sh        # staircase runner: warmup → measure → JSON
│   ├── scenarios/
│   │   ├── s1-discovery.lua
│   │   └── s2-client-credentials.lua
│   └── lib/
│       └── result-schema.md   # the JSON emitted into results/
├── reporting/
│   └── parse-wrk.py           # wrk stdout → structured JSON
└── results/                   # dated per-level JSON results (gitignored bulk)
```
