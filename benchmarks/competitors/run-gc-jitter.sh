#!/usr/bin/env bash
# benchmarks/competitors/run-gc-jitter.sh — GC-jitter long run (design D6).
#
# 5-minute P99 time series: N serial 10s wrk segments at fixed concurrency
# (default c=32), each parsed into p99/p50/qps, emitted as one JSON with a
# per-segment time series. The comparison plot (Fulla flat line vs JVM/Go
# GC spikes) is generated from these files by gen-comparison.py.
#
# Usage:
#   bash run-gc-jitter.sh --target http://127.0.0.1:8080 --ready-path /health/ready \
#       --product keycloak --product-version 26.7.1 \
#       --scenario .../scenarios/s6-userinfo.lua --lib-dir .../keycloak/lib \
#       --out-dir benchmarks/competitors/results
#
# Options: --segments 30 --seg-secs 10 --conn 32 --pre-secs 0 (pre-heat load)
#          --discard-spikes 5.0   (spike threshold: P99 > N*median -> contaminated)
#          --env-monitor          (background vmstat + meminfo collection)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PARSE="$REPO_ROOT/benchmarks/reporting/parse-wrk.py"

TARGET="" READY_PATH="/health/ready" PRODUCT="" PRODUCT_VERSION=""
SCENARIO="" LIB_DIR="" OUT_DIR=""
SEGMENTS=30 SEG_SECS=10 CONN=32 PRE_SECS=0
DISCARD_SPIKES=0.0 ENV_MONITOR=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --target) TARGET="$2"; shift 2 ;;
        --ready-path) READY_PATH="$2"; shift 2 ;;
        --product) PRODUCT="$2"; shift 2 ;;
        --product-version) PRODUCT_VERSION="$2"; shift 2 ;;
        --scenario) SCENARIO="$2"; shift 2 ;;
        --lib-dir) LIB_DIR="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --segments) SEGMENTS="$2"; shift 2 ;;
        --seg-secs) SEG_SECS="$2"; shift 2 ;;
        --conn) CONN="$2"; shift 2 ;;
        --pre-secs) PRE_SECS="$2"; shift 2 ;;
        --discard-spikes) DISCARD_SPIKES="$2"; shift 2 ;;
        --env-monitor) ENV_MONITOR=1; shift ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done
for req in TARGET PRODUCT SCENARIO LIB_DIR OUT_DIR; do
    [ -n "${!req}" ] || { echo "missing --$(echo "$req" | tr 'A-Z' 'a-z' | tr _ -)" >&2; exit 2; }
done

# threads: same heuristic as run-scenario.sh
CPU_CORES="$(nproc 2>/dev/null || echo 4)"
THREADS=$(( (CONN + 15) / 16 ))
[ "$THREADS" -gt "$CPU_CORES" ] && THREADS="$CPU_CORES"
[ "$THREADS" -lt 1 ] && THREADS=1

CODE="$(curl -sk -o /dev/null -w '%{http_code}' "$TARGET$READY_PATH" 2>/dev/null || echo 000)"
[ "$CODE" = "200" ] || { echo "[gcjitter] ERROR: target not ready at $TARGET$READY_PATH (code=$CODE)"; exit 1; }

mkdir -p "$OUT_DIR"
GIT_SHA="$(cd "$REPO_ROOT" && git rev-parse --short HEAD 2>/dev/null || echo nogit)"
DATE_TAG="$(date -u +%Y%m%d)"
WRK_VERSION="$(wrk --version 2>&1 | head -1 || true)"

# optional pre-heat segment (discarded)
if [ "$PRE_SECS" -gt 0 ]; then
    echo "[gcjitter] pre-heat ${PRE_SECS}s (discarded)..."
    WRK_LIB_DIR="$LIB_DIR" WRK_NTHREADS="$THREADS" wrk -t"$THREADS" -c"$CONN" \
        -d"${PRE_SECS}s" -s "$SCENARIO" "$TARGET" >/dev/null 2>&1 || true
fi

SERIES_FILE="$(mktemp)"
FAILS=0

# --- environment monitor (optional background collectors) ---
ENV_MONITOR_PID=""
ENV_MONITOR_DIR=""
MEMINFO_PID=""
if [ "$ENV_MONITOR" -eq 1 ]; then
    ENV_MONITOR_DIR="$(mktemp -d)"
    echo "[gcjitter] env-monitor started (vmstat + meminfo -> $ENV_MONITOR_DIR)"
    TOTAL_DUR=$(( SEGMENTS * SEG_SECS + PRE_SECS + 30 ))
    vmstat 1 "$TOTAL_DUR" > "$ENV_MONITOR_DIR/vmstat.log" 2>/dev/null &
    ENV_MONITOR_PID=$!
    ( while true; do cat /proc/meminfo >> "$ENV_MONITOR_DIR/meminfo.log" 2>/dev/null; echo "---" >> "$ENV_MONITOR_DIR/meminfo.log"; sleep 5; done ) &
    MEMINFO_PID=$!
fi

echo "[gcjitter] product=$PRODUCT c=$CONN segments=$SEGMENTS×${SEG_SECS}s scenario=$(basename "$SCENARIO")"
for i in $(seq 0 $((SEGMENTS - 1))); do
    T_START="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    WRK_OUT="$(mktemp)"
    WRK_LIB_DIR="$LIB_DIR" WRK_NTHREADS="$THREADS" \
        wrk -t"$THREADS" -c"$CONN" -d"${SEG_SECS}s" --latency -s "$SCENARIO" "$TARGET" \
        >"$WRK_OUT" 2>&1 || true
    python3 "$PARSE" --scenario "$(basename "$SCENARIO" .lua)" \
        --concurrency "$CONN" --threads "$THREADS" --duration "$SEG_SECS" \
        --product "$PRODUCT" ${PRODUCT_VERSION:+--product-version "$PRODUCT_VERSION"} \
        --wrk-version "$WRK_VERSION" < "$WRK_OUT" > "$WRK_OUT.json" 2>/dev/null || true
    rm -f "$WRK_OUT"
    if [ -s "$WRK_OUT.json" ] && python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if d.get("latency_us") else 1)' "$WRK_OUT.json" 2>/dev/null; then
        python3 - "$WRK_OUT.json" "$i" "$T_START" >> "$SERIES_FILE" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
lat = d.get("latency_us") or {}
q = d.get("qps")
err = d.get("error_rate")
print(json.dumps({
    "i": int(sys.argv[2]),
    "t_start": sys.argv[3],
    "p50_us": lat.get("p50"),
    "p99_us": lat.get("p99"),
    "qps": q,
    "error_rate": err,
}))
PY
        echo "  seg $((i + 1))/$SEGMENTS  p99=$(python3 -c 'import json;d=json.loads(open(0).read());print((((d.get("latency_us") or {}).get("p99")) or 0)/1000)' <"$WRK_OUT.json" 2>/dev/null || echo '?')ms"
    else
        FAILS=$((FAILS + 1))
        echo "  seg $((i + 1))/$SEGMENTS  FAILED (no parseable output)"
        python3 - "$i" "$T_START" >> "$SERIES_FILE" <<'PY'
import json, sys
print(json.dumps({"i": int(sys.argv[1]), "t_start": sys.argv[2],
                  "p50_us": None, "p99_us": None, "qps": None, "error_rate": None}))
PY
    fi
    rm -f "$WRK_OUT.json"
done

# --- stop env monitor ---
if [ -n "$ENV_MONITOR_PID" ]; then
    kill "$ENV_MONITOR_PID" 2>/dev/null || true
    wait "$ENV_MONITOR_PID" 2>/dev/null || true
fi
if [ -n "${MEMINFO_PID:-}" ]; then
    kill "$MEMINFO_PID" 2>/dev/null || true
    wait "$MEMINFO_PID" 2>/dev/null || true
fi

OUT_FILE="$OUT_DIR/${DATE_TAG}-${GIT_SHA}-${PRODUCT}-gcjitter.json"
python3 - "$SERIES_FILE" "$OUT_FILE" "$PRODUCT" "$PRODUCT_VERSION" "$CONN" "$THREADS" \
    "$SEGMENTS" "$SEG_SECS" "$(basename "$SCENARIO" .lua)" "$GIT_SHA" "$WRK_VERSION" \
    "$DISCARD_SPIKES" "${ENV_MONITOR_DIR:-}" <<'PY'
import json, sys, statistics, os

series = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
discard_threshold = float(sys.argv[11])
env_monitor_dir = sys.argv[12] if len(sys.argv) > 12 and sys.argv[12] else ""

# --- spike detection ---
valid_p99s = [s["p99_us"] for s in series if s.get("p99_us") is not None]
median_p99 = statistics.median(valid_p99s) if valid_p99s else 0
spike_threshold = median_p99 * discard_threshold if discard_threshold > 0 else float("inf")

contaminated_indices = []
clean_series = []
for s in series:
    p99 = s.get("p99_us")
    is_contaminated = (p99 is not None and discard_threshold > 0 and p99 > spike_threshold)
    if is_contaminated:
        contaminated_indices.append(s["i"])
    else:
        clean_series.append(s)

# --- cleaned stats (after removing contaminated segments) ---
cleaned_stats = None
if contaminated_indices and clean_series:
    clean_p99s = [s["p99_us"] for s in clean_series if s.get("p99_us") is not None]
    clean_p50s = [s["p50_us"] for s in clean_series if s.get("p50_us") is not None]
    clean_qps  = [s["qps"] for s in clean_series if s.get("qps") is not None]
    if clean_p99s:
        cleaned_stats = {
            "p50_us": round(statistics.median(clean_p50s), 1) if clean_p50s else None,
            "p99_us": round(statistics.median(clean_p99s), 1) if clean_p99s else None,
            "qps_avg": round(statistics.mean(clean_qps), 1) if clean_qps else None,
            "cleaned_segments": len(clean_series),
        }

# --- env noise summary ---
env_noise = None
if discard_threshold > 0:
    env_noise = {
        "spike_threshold_multiplier": discard_threshold,
        "spike_threshold_us": round(spike_threshold, 1) if spike_threshold != float("inf") else None,
        "median_p99_us": round(median_p99, 1),
        "contaminated_count": len(contaminated_indices),
        "contaminated_ratio": round(len(contaminated_indices) / len(series), 4) if series else 0,
        "contaminated_indices": contaminated_indices,
    }

# --- env monitor data paths ---
env_monitor_info = None
if env_monitor_dir and os.path.isdir(env_monitor_dir):
    env_monitor_info = {
        "vmstat_log": os.path.join(env_monitor_dir, "vmstat.log"),
        "meminfo_log": os.path.join(env_monitor_dir, "meminfo.log"),
    }

doc = {
    "schema_version": 2,
    "kind": "gcjitter",
    "product": sys.argv[3],
    "product_version": sys.argv[4],
    "scenario": sys.argv[9],
    "concurrency": int(sys.argv[5]),
    "threads": int(sys.argv[6]),
    "segments": int(sys.argv[7]),
    "seg_secs": int(sys.argv[8]),
    "series": series,
    "env": {
        "git_sha": sys.argv[10],
        "wrk_version": sys.argv[11],
        "network": "localhost-cross-container",
    },
}
if env_noise:
    doc["env_noise"] = env_noise
if cleaned_stats:
    doc["cleaned_stats"] = cleaned_stats
if env_monitor_info:
    doc["env_monitor"] = env_monitor_info

json.dump(doc, open(sys.argv[2], "w"), indent=2)
print(f"[gcjitter] wrote {sys.argv[2]} ({len(series)} segments)")
if env_noise:
    print(f"[gcjitter] spike detection: threshold={env_noise['spike_threshold_us']}us "
          f"({discard_threshold}x median {env_noise['median_p99_us']}us), "
          f"contaminated={env_noise['contaminated_count']}/{len(series)} "
          f"({env_noise['contaminated_ratio']*100:.1f}%)")
if cleaned_stats:
    print(f"[gcjitter] cleaned stats (excl. env noise): "
          f"p50={cleaned_stats['p50_us']}us p99={cleaned_stats['p99_us']}us "
          f"qps={cleaned_stats['qps_avg']} ({cleaned_stats['cleaned_segments']} clean segments)")
PY
rm -f "$SERIES_FILE"
if [ -n "${ENV_MONITOR_DIR:-}" ] && [ -d "${ENV_MONITOR_DIR:-/nonexistent}" ]; then
    echo "[gcjitter] env-monitor data at: $ENV_MONITOR_DIR"
fi

[ "$FAILS" -le 3 ] || { echo "[gcjitter] ERROR: $FAILS failed segments (>3)"; exit 1; }
exit 0
