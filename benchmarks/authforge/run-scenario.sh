#!/usr/bin/env bash
# benchmarks/authforge/run-scenario.sh
#
# Staircase load runner: for each concurrency level, run a short warmup
# (discarded) then a measured run, parse wrk output into a structured JSON in
# benchmarks/results/. Part of benchmark-facility-design.md M1 (§5.1–5.3).
#
# Usage:
#   bash benchmarks/authforge/run-scenario.sh <scenario.lua> [conn ...]
#   bash benchmarks/authforge/run-scenario.sh scenarios/s2-client-credentials.lua 2 4 8
#   bash benchmarks/authforge/run-scenario.sh scenarios/s1-discovery.lua   # default staircase
#
# Defaults: staircase 2 4 8 16 32 64 128 256; 10s warmup; 30s measured run.
#
# Env overrides:
#   TARGET_URL   default http://127.0.0.1:5555
#   WARMUP_S     default 10
#   DURATION_S   default 30
#   DRIVER_CPU_GATE  default 80  (wrk CPU at/above this marks the level "limited")
#
# Each level produces: results/<date>-<sha>-<scenario>-c<conn>.json
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$BENCH_DIR/../.." && pwd)"
TARGET_URL="${TARGET_URL:-http://127.0.0.1:5555}"
WARMUP_S="${WARMUP_S:-10}"
DURATION_S="${DURATION_S:-30}"
DRIVER_CPU_GATE="${DRIVER_CPU_GATE:-80}"
RESULTS_DIR="$REPO_ROOT/benchmarks/results"
PARSE="$REPO_ROOT/benchmarks/reporting/parse-wrk.py"

# --- resolve + validate the scenario script ---
SCENARIO_REL="${1:-}"
if [ -z "$SCENARIO_REL" ]; then
    echo "Usage: bash benchmarks/authforge/run-scenario.sh <scenario.lua> [conn ...]"
    echo "Scenarios: $BENCH_DIR/scenarios/"
    exit 2
fi
# accept either a path relative to repo root or one relative to the bench dir
if [ -f "$REPO_ROOT/$SCENARIO_REL" ]; then
    SCENARIO_PATH="$REPO_ROOT/$SCENARIO_REL"
elif [ -f "$BENCH_DIR/$SCENARIO_REL" ]; then
    SCENARIO_PATH="$BENCH_DIR/$SCENARIO_REL"
else
    echo "[run] ERROR: scenario not found: $SCENARIO_REL"
    echo "       looked in: $REPO_ROOT/$SCENARIO_REL and $BENCH_DIR/$SCENARIO_REL"
    exit 2
fi
SCENARIO_NAME="$(basename "$SCENARIO_PATH" .lua)"
shift || true
if [ "$#" -gt 0 ]; then
    LEVELS=("$@")
else
    # default staircase (design §5.1)
    read -r -a LEVELS <<< "2 4 8 16 32 64 128 256"
fi

# --- dependency checks ---
if ! command -v wrk >/dev/null 2>&1; then
    echo "[run] ERROR: wrk not found on PATH."
    echo "       Debian/Ubuntu: sudo apt-get install -y wrk"
    echo "       macOS:         brew install wrk"
    echo "       Alpine:        apk add --no-cache wrk"
    exit 2
fi
WRK_VERSION="$(wrk --version 2>&1 | head -1 || true)"
if ! command -v python3 >/dev/null 2>&1; then
    echo "[run] ERROR: python3 not found (needed for parse-wrk.py)."
    exit 2
fi

# --- target reachability gate ---
CODE="$(curl -s -o /dev/null -w '%{http_code}' "$TARGET_URL/health/ready" 2>/dev/null || echo 000)"
if [ "$CODE" != "200" ]; then
    echo "[run] ERROR: target not ready at $TARGET_URL/health/ready (code=$CODE)."
    echo "       Run bash benchmarks/authforge/setup.sh first."
    exit 1
fi

# --- env metadata for result files ---
GIT_SHA="$(cd "$REPO_ROOT" && git rev-parse --short HEAD 2>/dev/null || echo nogit)"
GIT_BRANCH="$(cd "$REPO_ROOT" && git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
DATE_TAG="$(date -u +%Y%m%d)"
export BENCH_GIT_SHA="$GIT_SHA" BENCH_GIT_BRANCH="$GIT_BRANCH"

# CPU count for the -t heuristic (min(cores, conns/16) rounded up)
CPU_CORES="$(nproc 2>/dev/null || echo 4)"

mkdir -p "$RESULTS_DIR"

echo "[run] scenario=$SCENARIO_NAME  target=$TARGET_URL  wrk=$WRK_VERSION"
echo "[run] levels: ${LEVELS[*]}  warmup=${WARMUP_S}s  measure=${DURATION_S}s  cpu_gate=${DRIVER_CPU_GATE}%"
echo "[run] results -> $RESULTS_DIR/"

for CONN in "${LEVELS[@]}"; do
    # -t heuristic (design §5.2): min(cores, conns/16) rounded up, at least 1.
    THREADS=$(( (CONN + 15) / 16 ))
    if [ "$THREADS" -gt "$CPU_CORES" ]; then THREADS="$CPU_CORES"; fi
    if [ "$THREADS" -lt 1 ]; then THREADS=1; fi

    echo ""
    echo "=== $SCENARIO_NAME  c=$CONN  t=$THREADS ==="

    # --- warmup (discarded) ---
    echo "  warmup ${WARMUP_S}s (discarded)..."
    wrk -t"$THREADS" -c"$CONN" -d"${WARMUP_S}s" -s "$SCENARIO_PATH" "$TARGET_URL" \
        >/dev/null 2>&1 || echo "  (warmup wrk rc=$?, continuing)"

    # --- measured run ---
    # Capture wrk in the background so we can sample its CPU% mid-run.
    WRK_OUT="$(mktemp)"
    wrk -t"$THREADS" -c"$CONN" -d"${DURATION_S}s" --latency -s "$SCENARIO_PATH" "$TARGET_URL" \
        >"$WRK_OUT" 2>&1 &
    WRK_PID=$!

    # Sample driver (wrk) CPU once mid-run. `ps` CPU% is lifetime-averaged over
    # the process, so sampling near the end of a 30s run approximates the
    # steady-state wrk load. This is deliberately a coarse gate (design §5.4).
    DRIVER_CPU=""
    sleep $(( DURATION_S > 5 ? DURATION_S - 3 : 1 ))
    if kill -0 "$WRK_PID" 2>/dev/null; then
        # ps -o %cpu gives a 0-100*cores range on some platforms; take the
        # per-core share by dividing if > 100. Good enough for the <80% gate.
        RAW_CPU="$(ps -o %cpu= -p "$WRK_PID" 2>/dev/null | tr -d ' ' || echo 0)"
        # normalize: if the platform reports aggregate (can exceed 100 on
        # multi-thread), reduce to a per-core-equivalent for the gate.
        DRIVER_CPU="$(python3 -c "v=float('${RAW_CPU:-0}' or 0)/max(1,$THREADS); print(round(v,1))" 2>/dev/null || echo "")"
    fi
    wait "$WRK_PID" || true
    WRK_RC=$?

    if [ "$WRK_RC" -ne 0 ]; then
        echo "  ERROR: wrk exited rc=$WRK_RC:"
        sed 's/^/    /' "$WRK_OUT"
        rm -f "$WRK_OUT"
        continue
    fi

    OUT_FILE="$RESULTS_DIR/${DATE_TAG}-${GIT_SHA}-${SCENARIO_NAME}-c${CONN}.json"
    # shellcheck disable=SC2086
    python3 "$PARSE" \
        --scenario "$SCENARIO_NAME" \
        --concurrency "$CONN" \
        --threads "$THREADS" \
        --duration "$DURATION_S" \
        ${DRIVER_CPU:+--driver-cpu "$DRIVER_CPU"} \
        --wrk-version "$WRK_VERSION" \
        < "$WRK_OUT" > "$OUT_FILE"
    rm -f "$WRK_OUT"

    # one-line summary from the JSON
    python3 - "$OUT_FILE" "$DRIVER_CPU_GATE" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
gate = float(sys.argv[2])
lat = r.get("latency_us") or {}
p99 = lat.get("p99")
p99_s = f"{p99/1e6:.3f}s" if p99 else "n/a"
qps = r.get("qps")
qps_s = f"{qps:,.0f}" if qps is not None else "n/a"
drv = r.get("driver") or {}
limited = " [DRIVER LIMITED]" if drv.get("limited") else ""
cpu = drv.get("cpu_pct")
cpu_s = f"{cpu}%" if cpu is not None else "n/a"
err = r.get("error_rate", 0) * 100
print(f"  -> {sys.argv[1].split('/')[-1]}: QPS={qps_s}  P99={p99_s}  "
      f"err={err:.4f}%  driver_cpu={cpu_s}{limited}")
PY
done

echo ""
echo "[run] done. results in: $RESULTS_DIR/"
