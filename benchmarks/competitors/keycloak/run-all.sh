#!/usr/bin/env bash
# benchmarks/competitors/keycloak/run-all.sh
#
# Full Keycloak suite for the comparison (design §5): 5 scenarios × staircase
# 2→128 (same params as the AuthForge baseline: 5s warmup / 10s measured — the
# Keycloak-specific 60s JIT heat happened in setup.sh), GC-jitter long run
# (S6 c=32, 30×10s, design D6), cold start (fresh / restart), RSS sampling
# during S2. Results land in benchmarks/competitors/results/.
#
# Assumes setup.sh already ran (stack up, pools minted).
set -euo pipefail

KC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$KC_DIR/../../.." && pwd)"
RUNNER="$REPO_ROOT/benchmarks/authforge/run-scenario.sh"
RESULTS_DIR="$REPO_ROOT/benchmarks/competitors/results"
mkdir -p "$RESULTS_DIR"

KC_URL="${KC_URL:-http://127.0.0.1:8080}"
KC_VERSION="$(cat "$KC_DIR/lib/generated/product_version.txt" 2>/dev/null || echo unknown)"
LEVELS=(2 4 8 16 32 64 128)

# Shared invocation env (M0 parameters): same runner, same staircase, same
# warmup/measure as AuthForge; product identity + result dir redirected.
export TARGET_URL="$KC_URL"
export READY_PATH="/realms/master"   # app-port probe (KC 26 health is on mgmt port 9000)
export WARMUP_S=5
export DURATION_S=10
export RESULTS_DIR
export WRK_LIB_DIR="$KC_DIR/lib"
export BENCH_PRODUCT=keycloak
export BENCH_PRODUCT_VERSION="$KC_VERSION"
export CONTAINER_GLOB='kc-bench-*'

run_staircase() {  # <scenario.lua> [extra runner args...]
    local lua="$1"; shift || true
    bash "$RUNNER" "$lua" "${LEVELS[@]}" "$@" 2>&1 | sed 's/^/  /'
}

echo "== S1 discovery =="
run_staircase "$KC_DIR/scenarios/s1-discovery.lua"

echo "== S2 client_credentials (+RSS sampling) =="
run_staircase "$KC_DIR/scenarios/s2-client-credentials.lua" --observe-stats

echo "== S3 introspect =="
run_staircase "$KC_DIR/scenarios/s3-introspect.lua"

echo "== S5 refresh_token (reissue per level) =="
run_staircase "$KC_DIR/scenarios/s5-refresh-token.lua" \
    --reissue "bash '$KC_DIR/reissue-rt-pool.sh'"

echo "== S6 userinfo =="
run_staircase "$KC_DIR/scenarios/s6-userinfo.lua"

echo "== GC jitter (S6 c=32, 30x10s) =="
bash "$REPO_ROOT/benchmarks/competitors/run-gc-jitter.sh" \
    --target "$KC_URL" --ready-path "/realms/master" \
    --product keycloak --product-version "$KC_VERSION" \
    --scenario "$KC_DIR/scenarios/s6-userinfo.lua" \
    --lib-dir "$KC_DIR/lib" --out-dir "$RESULTS_DIR"

echo "== cold start (fresh + restart) =="
bash "$REPO_ROOT/benchmarks/competitors/measure-cold-start.sh" \
    --product keycloak --product-version "$KC_VERSION" \
    --compose-dir "$KC_DIR" --project kc-bench --service keycloak \
    --target "$KC_URL" --ready-path "/realms/master" \
    --out-dir "$RESULTS_DIR"

echo "[run-all] done. results in $RESULTS_DIR/"
