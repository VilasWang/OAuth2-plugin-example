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
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PARSE="$REPO_ROOT/benchmarks/reporting/parse-wrk.py"

TARGET="" READY_PATH="/health/ready" PRODUCT="" PRODUCT_VERSION=""
SCENARIO="" LIB_DIR="" OUT_DIR=""
SEGMENTS=30 SEG_SECS=10 CONN=32 PRE_SECS=0
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

OUT_FILE="$OUT_DIR/${DATE_TAG}-${GIT_SHA}-${PRODUCT}-gcjitter.json"
python3 - "$SERIES_FILE" "$OUT_FILE" "$PRODUCT" "$PRODUCT_VERSION" "$CONN" "$THREADS" \
    "$SEGMENTS" "$SEG_SECS" "$(basename "$SCENARIO" .lua)" "$GIT_SHA" "$WRK_VERSION" <<'PY'
import json, sys
series = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
doc = {
    "schema_version": 1,
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
json.dump(doc, open(sys.argv[2], "w"), indent=2)
print(f"[gcjitter] wrote {sys.argv[2]} ({len(series)} segments)")
PY
rm -f "$SERIES_FILE"

[ "$FAILS" -le 3 ] || { echo "[gcjitter] ERROR: $FAILS failed segments (>3)"; exit 1; }
exit 0
