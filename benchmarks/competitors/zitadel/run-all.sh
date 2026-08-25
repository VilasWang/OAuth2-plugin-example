#!/usr/bin/env bash
# benchmarks/competitors/zitadel/run-all.sh — full Zitadel suite (design §5).
#
# Coverage per design M2 decision gates: S1/S2/S3 always; S6 only when the
# smoke gate confirmed service-user tokens are accepted at userinfo; S5 is
# N/A for Zitadel (machine users get no refresh tokens per RFC 6749 §4.4.3,
# and Zitadel removed the password grant — see COMPARISON.md limitations).
# GC-jitter carrier: S6 when available, else S2 (annotated).
set -euo pipefail

ZA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$ZA_DIR/../../.." && pwd)"
RUNNER="$REPO_ROOT/benchmarks/fulla/run-scenario.sh"
RESULTS_DIR="$REPO_ROOT/benchmarks/competitors/results"
mkdir -p "$RESULTS_DIR"

ZITADEL_URL="${ZITADEL_URL:-http://localhost:8080}"   # keep host == ExternalDomain (issuer alignment)
ZA_VERSION="$(cat "$ZA_DIR/lib/generated/product_version.txt" 2>/dev/null || echo unknown)"
LEVELS=(2 4 8 16 32 64 128)

export TARGET_URL="$ZITADEL_URL"
export READY_PATH="/debug/healthz"
export WARMUP_S=5
export DURATION_S=10
export RESULTS_DIR
export WRK_LIB_DIR="$ZA_DIR/lib"
export BENCH_PRODUCT=zitadel
export BENCH_PRODUCT_VERSION="$ZA_VERSION"
export CONTAINER_GLOB='zitadel-bench-*'

run_staircase() {
    local lua="$1"; shift || true
    bash "$RUNNER" "$lua" "${LEVELS[@]}" "$@" 2>&1 | sed 's/^/  /'
}

# --- projection settle gate ---
# setup.sh mints ~2000 tokens right before this runs; Zitadel's CQRS
# projections reduce those events in the background and the DB stays hot for
# a while. Starting the staircase during that catch-up poisons S1/S2 with
# context-canceled 500s (observed: 99.99% errors at S1 c=16 without this
# gate). Gate: a 3s discovery smoke at c=2 must show zero non-2xx and zero
# socket errors before the staircase starts.
echo "== projection settle gate (discovery smoke must be clean) =="
SETTLED=0
for attempt in $(seq 1 20); do
    OUT="$(WRK_LIB_DIR="$ZA_DIR/lib" wrk -t1 -c2 -d3s \
        -s "$ZA_DIR/scenarios/s1-discovery.lua" "$ZITADEL_URL" 2>&1 || true)"
    if ! echo "$OUT" | grep -qE "Non-2xx|Socket errors: [1-9]"; then
        SETTLED=1
        echo "   clean after attempt $attempt"
        break
    fi
    echo "   attempt $attempt: still settling ($(echo "$OUT" | grep -oE 'Non-2xx or 3xx responses: [0-9]+|Socket errors:.*' | head -2 | tr '\n' ' '))"
    sleep 15
done
[ "$SETTLED" = "1" ] || { echo "[run-all] ERROR: projections did not settle in 5min — aborting"; exit 1; }

echo "== S1 discovery =="
run_staircase "$ZA_DIR/scenarios/s1-discovery.lua"

echo "== S2 client_credentials (JWT profile) =="
run_staircase "$ZA_DIR/scenarios/s2-client-credentials.lua" --observe-stats

echo "== S3 introspect =="
run_staircase "$ZA_DIR/scenarios/s3-introspect.lua"

# S5: N/A — no headless refresh-token path for machine users (see header).

S6_OK=0
if curl -s -o /dev/null -w '%{http_code}' -H "Authorization: Bearer $(head -1 "$ZA_DIR/lib/generated/cc_tokens.txt")" \
        "$ZITADEL_URL/oidc/v1/userinfo" | grep -q 200; then
    S6_OK=1
fi

GC_SCENARIO="$ZA_DIR/scenarios/s2-client-credentials.lua"
GC_NOTE="carrier=S2 (S6 unavailable)"
if [ "$S6_OK" = "1" ]; then
    echo "== S6 userinfo =="
    run_staircase "$ZA_DIR/scenarios/s6-userinfo.lua"
    GC_SCENARIO="$ZA_DIR/scenarios/s6-userinfo.lua"
    GC_NOTE="carrier=S6"
else
    echo "== S6 userinfo: SKIPPED (service-user tokens rejected at userinfo — N/A, annotated) =="
fi

echo "== GC jitter (c=32, 30x10s, $GC_NOTE) =="
bash "$REPO_ROOT/benchmarks/competitors/run-gc-jitter.sh" \
    --target "$ZITADEL_URL" --ready-path "/debug/healthz" \
    --product zitadel --product-version "$ZA_VERSION" \
    --scenario "$GC_SCENARIO" \
    --lib-dir "$ZA_DIR/lib" --out-dir "$RESULTS_DIR"

echo "== cold start (fresh + restart) =="
bash "$REPO_ROOT/benchmarks/competitors/measure-cold-start.sh" \
    --product zitadel --product-version "$ZA_VERSION" \
    --compose-dir "$ZA_DIR" --project zitadel-bench --service zitadel \
    --target "$ZITADEL_URL" --ready-path "/debug/healthz" \
    --out-dir "$RESULTS_DIR"

echo "[run-all] done. results in $RESULTS_DIR/"
