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
#   bash benchmarks/authforge/run-scenario.sh scenarios/s5-refresh-token.lua --reseed lib/generated/bench_refresh_tokens.sql
#
# Options:
#   --reseed <sql_file>  Before each concurrency level, re-apply this SQL file
#                        (with a prepended TRUNCATE) to refresh the token pool.
#                        Used by S5 (refresh_token) where each RT is consumed once.
#   --reissue "<cmd>"    Generic reseed hook for competitor targets: run this
#                        shell command before the warmup AND before the measured
#                        run of each level (e.g. re-issue an API refresh-token
#                        pool). Competitor S5 uses this instead of --reseed.
#   --observe            Run resource observers (docker-stats + scrape-metrics)
#                        in parallel with each measured run (M3, design §5.4).
#   --observe-stats      docker-stats observer only (competitors have no
#                        /metrics endpoint, so scrape-metrics is AuthForge-only).
#   --observe-metrics    scrape-metrics observer only.
#
# Defaults: staircase 2 4 8 16 32 64 128 256; 10s warmup; 30s measured run.
#
# Env overrides:
#   TARGET_URL   default http://127.0.0.1:5555
#   READY_PATH   default /health/ready  (reachability gate probe path; competitor
#                stacks use their own ready endpoint, e.g. /realms/master)
#   WARMUP_S     default 10
#   DURATION_S   default 30
#   DRIVER_CPU_GATE  default 80  (wrk CPU at/above this marks the level "limited")
#   RESULTS_DIR  default <repo>/benchmarks/results (competitor runs redirect to
#                benchmarks/competitors/results/)
#   WRK_LIB_DIR  default <bench>/lib  (token-pool dir the Lua scenario reads;
#                competitor scenarios point this at their own lib/)
#   BENCH_PRODUCT          default authforge (recorded in result env block;
#                          non-authforge also adds a <product> infix to the
#                          result filename)
#   BENCH_PRODUCT_VERSION  default "" (image/version tag of the target product)
#
# Each level produces: results/<date>-<sha>-[-<product>-]<scenario>-c<conn>.json
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$BENCH_DIR/../.." && pwd)"

# --- source paths.env for COMPOSE_FILE_REL (needed by --reseed) ---
PATHS_ENV_FILE="$REPO_ROOT/paths.env"
if [ -f "$PATHS_ENV_FILE" ]; then
    set -a
    # shellcheck disable=SC1090
    source "$PATHS_ENV_FILE"
    set +a
fi

TARGET_URL="${TARGET_URL:-http://127.0.0.1:5555}"
READY_PATH="${READY_PATH:-/health/ready}"
WARMUP_S="${WARMUP_S:-10}"
DURATION_S="${DURATION_S:-30}"
DRIVER_CPU_GATE="${DRIVER_CPU_GATE:-80}"
RESULTS_DIR="${RESULTS_DIR:-$REPO_ROOT/benchmarks/results}"
BENCH_PRODUCT="${BENCH_PRODUCT:-authforge}"
BENCH_PRODUCT_VERSION="${BENCH_PRODUCT_VERSION:-}"
PARSE="$REPO_ROOT/benchmarks/reporting/parse-wrk.py"

# --- resolve + validate the scenario script ---
SCENARIO_REL="${1:-}"
if [ -z "$SCENARIO_REL" ]; then
    echo "Usage: bash benchmarks/authforge/run-scenario.sh <scenario.lua> [conn ...]"
    echo "Scenarios: $BENCH_DIR/scenarios/"
    exit 2
fi
# accept an absolute/CWD-relative path, or one relative to repo root or bench dir
if [ -f "$SCENARIO_REL" ]; then
    SCENARIO_PATH="$(cd "$(dirname "$SCENARIO_REL")" && pwd)/$(basename "$SCENARIO_REL")"
elif [ -f "$REPO_ROOT/$SCENARIO_REL" ]; then
    SCENARIO_PATH="$REPO_ROOT/$SCENARIO_REL"
elif [ -f "$BENCH_DIR/$SCENARIO_REL" ]; then
    SCENARIO_PATH="$BENCH_DIR/$SCENARIO_REL"
else
    echo "[run] ERROR: scenario not found: $SCENARIO_REL"
    echo "       looked in: $SCENARIO_REL (cwd), $REPO_ROOT/$SCENARIO_REL and $BENCH_DIR/$SCENARIO_REL"
    exit 2
fi
SCENARIO_NAME="$(basename "$SCENARIO_PATH" .lua)"
shift || true

# --- parse remaining args: --reseed <sql>, --reissue <cmd>, --observe*, conn levels ---
RESEED_SQL=""
REISSUE_CMD=""
OBSERVE_STATS=0
OBSERVE_METRICS=0
LEVELS=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --reseed)
            RESEED_SQL="$2"
            shift 2 || { echo "[run] ERROR: --reseed requires a SQL file argument"; exit 2; }
            ;;
        --reissue)
            REISSUE_CMD="$2"
            shift 2 || { echo "[run] ERROR: --reissue requires a command argument"; exit 2; }
            ;;
        --observe)
            OBSERVE_STATS=1
            OBSERVE_METRICS=1
            shift
            ;;
        --observe-stats)
            OBSERVE_STATS=1
            shift
            ;;
        --observe-metrics)
            OBSERVE_METRICS=1
            shift
            ;;
        --help|-h)
            head -50 "$0" | tail -48
            exit 0
            ;;
        *)
            LEVELS+=("$1")
            shift
            ;;
    esac
done
if [ "${#LEVELS[@]}" -eq 0 ]; then
    # default staircase (design §5.1)
    read -r -a LEVELS <<< "2 4 8 16 32 64 128 256"
fi

# --- validate --reseed SQL file if specified ---
RESEED_SQL_ABS=""
if [ -n "$RESEED_SQL" ]; then
    if [ -f "$REPO_ROOT/$RESEED_SQL" ]; then
        RESEED_SQL_ABS="$REPO_ROOT/$RESEED_SQL"
    elif [ -f "$BENCH_DIR/$RESEED_SQL" ]; then
        RESEED_SQL_ABS="$BENCH_DIR/$RESEED_SQL"
    elif [ -f "$RESEED_SQL" ]; then
        RESEED_SQL_ABS="$(cd "$(dirname "$RESEED_SQL")" && pwd)/$(basename "$RESEED_SQL")"
    else
        echo "[run] ERROR: --reseed SQL file not found: $RESEED_SQL"
        exit 2
    fi
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

# --- target reachability gate (-k: competitor targets may serve self-signed TLS) ---
CODE="$(curl -sk -o /dev/null -w '%{http_code}' "$TARGET_URL$READY_PATH" 2>/dev/null || echo 000)"
if [ "$CODE" != "200" ]; then
    echo "[run] ERROR: target not ready at $TARGET_URL$READY_PATH (code=$CODE)."
    echo "       Run the target's setup.sh first (or override READY_PATH)."
    exit 1
fi

# --- env metadata for result files ---
GIT_SHA="$(cd "$REPO_ROOT" && git rev-parse --short HEAD 2>/dev/null || echo nogit)"
GIT_BRANCH="$(cd "$REPO_ROOT" && git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
DATE_TAG="$(date -u +%Y%m%d)"
export BENCH_GIT_SHA="$GIT_SHA" BENCH_GIT_BRANCH="$GIT_BRANCH"

# Filename infix for competitor results: AuthForge keeps the historical
# <date>-<sha>-<scenario> naming; competitors add a product segment so their
# files coexist in the same results dir (design §八).
BENCH_PRODUCT_INFIX=""
if [ "$BENCH_PRODUCT" != "authforge" ]; then
    BENCH_PRODUCT_INFIX="-$BENCH_PRODUCT"
fi

# CPU count for the -t heuristic (min(cores, conns/16) rounded up).
# nproc is Linux; sysctl -n hw.ncpu covers macOS/BSD; fall back to 4.
CPU_CORES="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

mkdir -p "$RESULTS_DIR"

# Export env vars for Lua scenarios:
#   WRK_LIB_DIR — token-pool dir for the scenario's dofile/token files.
#                 Default is the AuthForge bench lib; competitor runs override
#                 it so their scenarios resolve THEIR pools, not AuthForge's.
#   WRK_NTHREADS — not known until per-level; set inside the loop below
export WRK_LIB_DIR="${WRK_LIB_DIR:-$BENCH_DIR/lib}"

# Auto-detect benchmark config swap: setup.sh backs up config.json to
# config.json.dev-backup and copies config.bench.json over it. Detect the
# backup file's presence to record the correct target_config in result JSONs.
# (BENCH_TARGET_CONFIG from setup.sh is lost across separate process invocations.)
if [ -f "$REPO_ROOT/apps/server/config/config.json.dev-backup" ]; then
    export BENCH_TARGET_CONFIG="config.bench.json"
fi

echo "[run] scenario=$SCENARIO_NAME  target=$TARGET_URL  wrk=$WRK_VERSION"
echo "[run] product=$BENCH_PRODUCT${BENCH_PRODUCT_VERSION:+ ($BENCH_PRODUCT_VERSION)}  levels: ${LEVELS[*]}  warmup=${WARMUP_S}s  measure=${DURATION_S}s  cpu_gate=${DRIVER_CPU_GATE}%"
if [ -n "$RESEED_SQL_ABS" ]; then
    echo "[run] reseed: $RESEED_SQL_ABS (before each level)"
fi
if [ -n "$REISSUE_CMD" ]; then
    echo "[run] reissue: $REISSUE_CMD (before each level's warmup + measured run)"
fi
if [ "$OBSERVE_STATS" -eq 1 ] || [ "$OBSERVE_METRICS" -eq 1 ]; then
    echo "[run] observe:$([ "$OBSERVE_STATS" -eq 1 ] && echo ' docker-stats')$([ "$OBSERVE_METRICS" -eq 1 ] && echo ' scrape-metrics') (parallel with measured runs)"
fi
echo "[run] results -> $RESULTS_DIR/"

for CONN in "${LEVELS[@]}"; do
    # -t heuristic (design §5.2): min(cores, conns/16) rounded up, at least 1.
    THREADS=$(( (CONN + 15) / 16 ))
    if [ "$THREADS" -gt "$CPU_CORES" ]; then THREADS="$CPU_CORES"; fi
    if [ "$THREADS" -lt 1 ]; then THREADS=1; fi

    # S4 auth_code: the multi-step flow carries state across request() calls
    # (code from login → token exchange). wrk shares file-scope state across
    # connections within a thread, so this scenario MUST use -t == -c (one
    # connection per thread) to avoid interleaving corruption.
    if [ "$SCENARIO_NAME" = "s4-auth-code" ] && [ "$THREADS" != "$CONN" ]; then
        echo "  [s4] adjusting -t from $THREADS to $CONN (multi-step flow requires -t == -c)"
        THREADS=$CONN
    fi

    # Export the thread count for Lua scenarios (needed for pool slicing).
    export WRK_NTHREADS="$THREADS"

    echo ""
    echo "=== $SCENARIO_NAME  c=$CONN  t=$THREADS ==="

    # --- re-seed token pool (if --reseed was specified) ---
    # For S5 (refresh_token): each RT is consumed once, so the pool depletes.
    # Re-apply the SQL with a prepended TRUNCATE to get a fresh pool per level.
    if [ -n "$RESEED_SQL_ABS" ]; then
        # Resolve the postgres container by name (more reliable than compose ps -q,
        # which may fail if the project name or compose args don't match the stack
        # that setup.sh used to boot).
        PG_CONTAINER="$(docker ps -q --filter 'name=oauth2-postgres' --filter 'status=running' 2>/dev/null | head -1)"
        if [ -n "$PG_CONTAINER" ]; then
            # Prepend TRUNCATE to clear old (consumed/revoked) tokens, then INSERT fresh.
            { echo "TRUNCATE oauth2_refresh_tokens RESTART IDENTITY CASCADE;"; cat "$RESEED_SQL_ABS"; } \
                | docker exec -i "$PG_CONTAINER" psql -U oauth2_user -d oauth2_db -v ON_ERROR_STOP=1 -q >/dev/null 2>&1 \
                && echo "  reseed OK ($(basename "$RESEED_SQL_ABS"))" \
                || echo "  reseed WARN: failed, pool may be stale"
        else
            echo "  reseed WARN: postgres container not found"
        fi
    fi

    # --- generic reissue hook (competitor S5): refresh the API-issued pool ---
    # Runs BEFORE the warmup so warmup consumption doesn't eat the measured
    # window's pool; runs again before the measured run (see below).
    # WRK_LEVEL_CONN / WRK_REISSUE_PHASE let the hook size the pool for the
    # upcoming phase (warmup+measure vs measure only).
    if [ -n "$REISSUE_CMD" ]; then
        export WRK_LEVEL_CONN="$CONN" WRK_REISSUE_PHASE="pre-warmup"
        echo "  reissue (pre-warmup, c=$CONN)..."
        bash -c "$REISSUE_CMD" \
            && echo "  reissue OK" \
            || echo "  reissue WARN: rc=$? (pool may be stale)"
    fi

    # --- warmup (discarded) ---
    echo "  warmup ${WARMUP_S}s (discarded)..."
    wrk -t"$THREADS" -c"$CONN" -d"${WARMUP_S}s" -s "$SCENARIO_PATH" "$TARGET_URL" \
        >/dev/null 2>&1 || echo "  (warmup wrk rc=$?, continuing)"

    # --- re-seed again before the measured run (warmup consumed some RTs) ---
    # Re-resolve PG_CONTAINER in case the container was recreated during warmup.
    if [ -n "$RESEED_SQL_ABS" ]; then
        PG_CONTAINER_FRESH="$(docker ps -q --filter 'name=oauth2-postgres' --filter 'status=running' 2>/dev/null | head -1)"
        if [ -n "$PG_CONTAINER_FRESH" ]; then
            { echo "TRUNCATE oauth2_refresh_tokens RESTART IDENTITY CASCADE;"; cat "$RESEED_SQL_ABS"; } \
                | docker exec -i "$PG_CONTAINER_FRESH" psql -U oauth2_user -d oauth2_db -v ON_ERROR_STOP=1 -q >/dev/null 2>&1 \
                || echo "  reseed WARN: pre-measure reseed failed"
        else
            echo "  reseed WARN: postgres container not found before measured run"
        fi
    fi

    # --- reissue again before the measured run (warmup consumed pool items) ---
    if [ -n "$REISSUE_CMD" ]; then
        export WRK_LEVEL_CONN="$CONN" WRK_REISSUE_PHASE="pre-measure"
        echo "  reissue (pre-measure, c=$CONN)..."
        bash -c "$REISSUE_CMD" \
            && echo "  reissue OK" \
            || echo "  reissue WARN: rc=$? (pool may be stale)"
    fi

    # --- observe: start background resource collectors (M3, design §5.4) ---
    # Started AFTER warmup + reseed/reissue so the collection window exactly
    # covers the measured run (not the discarded warmup). Each observer
    # self-terminates after DURATION_S seconds; run-scenario.sh also kills
    # them after wrk finishes.
    STATS_FILE=""
    METRICS_FILE=""
    STATS_PID=""
    METRICS_PID=""
    if [ "$OBSERVE_STATS" -eq 1 ]; then
        STATS_FILE="$RESULTS_DIR/${DATE_TAG}-${GIT_SHA}${BENCH_PRODUCT_INFIX}-${SCENARIO_NAME}-c${CONN}-docker-stats.tsv"
        bash "$BENCH_DIR/observe/docker-stats.sh" "$STATS_FILE" "${DURATION_S}s" &
        STATS_PID=$!
    fi
    if [ "$OBSERVE_METRICS" -eq 1 ]; then
        METRICS_FILE="$RESULTS_DIR/${DATE_TAG}-${GIT_SHA}${BENCH_PRODUCT_INFIX}-${SCENARIO_NAME}-c${CONN}-metrics.txt"
        bash "$BENCH_DIR/observe/scrape-metrics.sh" "$TARGET_URL" "$METRICS_FILE" "${DURATION_S}s" &
        METRICS_PID=$!
    fi

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
    # capture the real wrk exit status: `$?` after `|| true` is always 0,
    # which made the WRK_RC branch below dead code (review finding)
    WRK_RC=0
    wait "$WRK_PID" || WRK_RC=$?

    # --- stop background resource collectors after the measured run ---
    if [ -n "${STATS_PID:-}" ]; then
        kill "$STATS_PID" 2>/dev/null || true
        [ -f "$STATS_FILE" ] && echo "  docker-stats → $(basename "$STATS_FILE")"
    fi
    if [ -n "${METRICS_PID:-}" ]; then
        kill "$METRICS_PID" 2>/dev/null || true
        [ -f "$METRICS_FILE" ] && echo "  metrics      → $(basename "$METRICS_FILE")"
    fi

    if [ "$WRK_RC" -ne 0 ]; then
        echo "  ERROR: wrk exited rc=$WRK_RC:"
        sed 's/^/    /' "$WRK_OUT"
        rm -f "$WRK_OUT"
        continue
    fi

    OUT_FILE="$RESULTS_DIR/${DATE_TAG}-${GIT_SHA}${BENCH_PRODUCT_INFIX}-${SCENARIO_NAME}-c${CONN}.json"
    # shellcheck disable=SC2086
    python3 "$PARSE" \
        --scenario "$SCENARIO_NAME" \
        --concurrency "$CONN" \
        --threads "$THREADS" \
        --duration "$DURATION_S" \
        ${DRIVER_CPU:+--driver-cpu "$DRIVER_CPU"} \
        --wrk-version "$WRK_VERSION" \
        --product "$BENCH_PRODUCT" \
        ${BENCH_PRODUCT_VERSION:+--product-version "$BENCH_PRODUCT_VERSION"} \
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
