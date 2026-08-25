#!/usr/bin/env bash
# benchmarks/competitors/run-fulla-session.sh
#
# Fulla side of the same-session comparison (design §5.1 v1.1): re-run the
# staircase (S1/S2/S3/S5/S6 — S4 excluded from the comparison, D4), GC jitter
# (S6 c=32, D6), RSS sampling and cold start in the SAME machine session as
# the competitor stacks, eliminating cross-day environment drift (risk R7).
# Results land in the canonical benchmarks/results/ (no product infix;
# distinguished by fresh date+sha) + competitors/results/ for gcjitter.
#
# Usage:
#   bash benchmarks/competitors/run-fulla-session.sh
#
# Assumes: docker stack down (run-fulla-session calls fulla setup.sh
# itself, which tears down any previous bench stack first).
set -euo pipefail

COMP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$COMP_DIR/../.." && pwd)"
AF_BENCH="$REPO_ROOT/benchmarks/fulla"
RUNNER="$AF_BENCH/run-scenario.sh"
RESULTS_DIR="$REPO_ROOT/benchmarks/results"     # canonical Fulla location
COMP_RESULTS="$COMP_DIR/results"

export TARGET_URL="${TARGET_URL:-http://127.0.0.1:5555}"
export READY_PATH="/health/ready"
export WARMUP_S=5
export DURATION_S=10
export RESULTS_DIR
export BENCH_PRODUCT=fulla
export BENCH_PRODUCT_VERSION="$(cd "$REPO_ROOT" && git rev-parse --short HEAD)"

echo "== Fulla benchmark stack setup =="
bash "$AF_BENCH/setup.sh"

echo "== S1 discovery =="
bash "$RUNNER" "$AF_BENCH/scenarios/s1-discovery.lua" 2 4 8 16 32 64 128 2>&1 | sed 's/^/  /'

echo "== S2 client_credentials (+RSS sampling) =="
bash "$RUNNER" "$AF_BENCH/scenarios/s2-client-credentials.lua" 2 4 8 16 32 64 128 \
    --observe-stats 2>&1 | sed 's/^/  /'

echo "== S3 introspect =="
bash "$RUNNER" "$AF_BENCH/scenarios/s3-introspect.lua" 2 4 8 16 32 64 128 2>&1 | sed 's/^/  /'

echo "== S5 refresh_token (SQL reseed per level) =="
bash "$RUNNER" "$AF_BENCH/scenarios/s5-refresh-token.lua" 2 4 8 16 32 64 128 \
    --reseed "$AF_BENCH/lib/generated/bench_refresh_tokens.sql" 2>&1 | sed 's/^/  /'

echo "== S6 userinfo =="
bash "$RUNNER" "$AF_BENCH/scenarios/s6-userinfo.lua" 2 4 8 16 32 64 128 2>&1 | sed 's/^/  /'

echo "== GC jitter (S6 c=32, 30x10s) =="
bash "$COMP_DIR/run-gc-jitter.sh" \
    --target "$TARGET_URL" --ready-path "/health/ready" \
    --product fulla --product-version "$BENCH_PRODUCT_VERSION" \
    --scenario "$AF_BENCH/scenarios/s6-userinfo.lua" \
    --lib-dir "$AF_BENCH/lib" --out-dir "$COMP_RESULTS"

echo "== cold start (fresh + pre-migrated, Fulla facility) =="
bash "$AF_BENCH/measure-cold-start.sh"
bash "$AF_BENCH/measure-cold-start.sh" --pre-migrated

echo "== teardown =="
bash "$AF_BENCH/teardown.sh" || true
echo "[fulla-session] done."
