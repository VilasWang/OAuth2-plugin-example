# Benchmark result JSON schema

Every `run-scenario.sh` level produces one JSON file in `benchmarks/results/`,
named `<YYYYMMDD>-<git-sha>-<scenario>-c<conn>.json` (Fulla) or
`<YYYYMMDD>-<git-sha>-<product>-<scenario>-c<conn>.json` (competitor runs via
`BENCH_PRODUCT`). The file is written by
`benchmarks/reporting/parse-wrk.py` and conforms to `schema_version: 1` below.
M4's `gen-summary.py` aggregates these into `results/SUMMARY.md`.

## Schema (v1)

| Field | Type | Meaning |
|---|---|---|
| `schema_version` | int | `1` |
| `scenario` | string | e.g. `s2-client-credentials` (basename of the `.lua`) |
| `concurrency` | int | wrk `-c` (connections) for this level |
| `threads` | int | wrk `-t` (threads), `min(cores, conns/16)` rounded up |
| `duration_s` | int | measured-run length (default 30) |
| `qps` | float\|null | `Requests/sec` — primary throughput metric |
| `latency_us` | object | percentile → microseconds: `p50`/`p75`/`p90`/`p95`/`p99`, and `max` when wrk emits it. All normalised to µs regardless of wrk's printed unit |
| `total_requests` | int\|null | `N requests in T s` |
| `error_rate` | float | `(non_2xx + socket_errors) / total_requests`; `0.0` when clean |
| `error_count` | int | `non_2xx + Σ socket_errors` |
| `socket_errors` | object | `{connect,read,write,timeout}` — absent fields ⇒ no socket errors |
| `non_2xx_responses` | int\|null | wrk's `Non-2xx or 3xx responses` line; `null` if absent (clean run) |
| `driver.cpu_pct` | float\|null | sampled wrk-process CPU (per-core-normalised); `null` if unmeasured |
| `driver.limited` | bool | `true` when `cpu_pct ≥ 80` (AC4 driver-credibility gate) |
| `env` | object | metadata block (below) |

### `env` metadata block

| Field | Source | Purpose |
|---|---|---|
| `date` | auto (UTC ISO-8601) | when the run happened |
| `git_sha` | `git rev-parse --short HEAD` | reproducibility |
| `git_branch` | `git rev-parse --abbrev-ref HEAD` | context |
| `product` | `--product` / `BENCH_PRODUCT` env, default `fulla` | which product was load-tested (`fulla`/`keycloak`/`ory`/`zitadel`); competitor comparison (Phase 0.5) keys off this |
| `product_version` | `--product-version` / `BENCH_PRODUCT_VERSION` env | target product version or image tag |
| `target_image` | `BENCH_TARGET_IMAGE` env | which container image was load-tested |
| `target_config` | default `config.json` | which shipped config |
| `target_spec` | `BENCH_TARGET_SPEC` env | target machine vCPU/RAM |
| `driver_spec` | `BENCH_DRIVER_SPEC` env | driver machine vCPU/RAM |
| `network` | default `localhost-cross-container` | topology (affects latency floor) |
| `wrk_version` | `wrk --version` | tool provenance |

### Notes

- `latency_us` keys are **microseconds**, even when wrk printed `ms` or `s`. The
  parser normalises via `ms→×1000`, `s→×1_000_000`.
- `qps`, `total_requests`, `latency_us.p*` can be `null`/absent if wrk's output
  was malformed — downstream tooling must tolerate this.
- Socket-error and non-2xx lines are **optional** in wrk output; they only
  appear when such errors occurred. The parser fills `0`/`null` otherwise.
