#!/usr/bin/env bash
# benchmarks/competitors/ory/run-all.sh — full Ory Hydra suite (design §5).
# S3 runs against the ADMIN port (4445) — semantic annotation in COMPARISON.md.
set -euo pipefail

ORY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$ORY_DIR/../../.." && pwd)"
RUNNER="$REPO_ROOT/benchmarks/authforge/run-scenario.sh"
RESULTS_DIR="$REPO_ROOT/benchmarks/competitors/results"
mkdir -p "$RESULTS_DIR"

PUBLIC_URL="${PUBLIC_URL:-http://127.0.0.1:4444}"
ADMIN_URL="${ADMIN_URL:-http://127.0.0.1:4445}"
HYDRA_VERSION="$(cat "$ORY_DIR/lib/generated/product_version.txt" 2>/dev/null || echo unknown)"
LEVELS=(2 4 8 16 32 64 128)

export READY_PATH="/health/ready"
export WARMUP_S=5
export DURATION_S=10
export RESULTS_DIR
export WRK_LIB_DIR="$ORY_DIR/lib"
export BENCH_PRODUCT=ory
export BENCH_PRODUCT_VERSION="$HYDRA_VERSION"
export CONTAINER_GLOB='ory-bench-*'

run_staircase() {
    local lua="$1"; shift || true
    bash "$RUNNER" "$lua" "${LEVELS[@]}" "$@" 2>&1 | sed 's/^/  /'
}

echo "== S1 discovery (public :4444) =="
TARGET_URL="$PUBLIC_URL" run_staircase "$ORY_DIR/scenarios/s1-discovery.lua"

echo "== S2 client_credentials (public :4444, +RSS sampling) =="
TARGET_URL="$PUBLIC_URL" run_staircase "$ORY_DIR/scenarios/s2-client-credentials.lua" --observe-stats

echo "== S3 introspect (ADMIN :4445 — D3 annotation) =="
TARGET_URL="$ADMIN_URL" run_staircase "$ORY_DIR/scenarios/s3-introspect.lua"

echo "== S5 refresh_token (public :4444, reissue per level) =="
TARGET_URL="$PUBLIC_URL" run_staircase "$ORY_DIR/scenarios/s5-refresh-token.lua" \
    --reissue "bash '$ORY_DIR/reissue-rt-pool.sh'"

echo "== S6 userinfo (public :4444) =="
TARGET_URL="$PUBLIC_URL" run_staircase "$ORY_DIR/scenarios/s6-userinfo.lua"

echo "== GC jitter (S6 c=32, 30x10s) =="
bash "$REPO_ROOT/benchmarks/competitors/run-gc-jitter.sh" \
    --target "$PUBLIC_URL" --ready-path "/health/ready" \
    --product ory --product-version "$HYDRA_VERSION" \
    --scenario "$ORY_DIR/scenarios/s6-userinfo.lua" \
    --lib-dir "$ORY_DIR/lib" --out-dir "$RESULTS_DIR"

echo "== cold start (fresh + restart) =="
bash "$REPO_ROOT/benchmarks/competitors/measure-cold-start.sh" \
    --product ory --product-version "$HYDRA_VERSION" \
    --compose-dir "$ORY_DIR" --project ory-bench --service hydra \
    --target "$PUBLIC_URL" --ready-path "/health/ready" \
    --out-dir "$RESULTS_DIR"

echo "[run-all] done. results in $RESULTS_DIR/"
