#!/usr/bin/env bash
# observe/scrape-metrics.sh — poll /metrics during a benchmark run.
#
# Part of benchmark-facility-design.md M3 (§5.4 resource observation).
# The backend exposes a Prometheus exporter at /metrics (config.json:135-138,
# PromExporter plugin). We poll it at 1s intervals and write each scrape
# (timestamped) to the output file. The actual labels are {endpoint},
# {client_id}, {error} — NOT {http_status} (see Metrics.cc).
# NOTE: config.bench.json no longer loads PromExporter (quick-win micro-opt,
# noncode-performance-optimization.md §二.5) — /metrics returns 404 under the
# bench config. Use --observe-metrics only with a config that keeps the plugin
# (dev config.json, or re-add the plugin to the bench config).
#
# Usage (typically invoked by run-scenario.sh --observe):
#   bash observe/scrape-metrics.sh <target_url> <output.txt> [duration]
#
# Arguments:
#   target_url  — e.g. http://127.0.0.1:5555
#   output.txt  — output file path
#   duration    — stop after this long (e.g. "30s"); default: run until killed

set -euo pipefail

TARGET_URL="${1:-}"
OUT_FILE="${2:-}"
DURATION="${3:-}"

if [ -z "$TARGET_URL" ] || [ -z "$OUT_FILE" ]; then
    echo "Usage: bash observe/scrape-metrics.sh <target_url> <output.txt> [duration]"
    exit 2
fi

# Parse duration (e.g. "30s" → 30, "2m" → 120). Empty = infinite.
MAX_SECONDS=0
if [ -n "$DURATION" ]; then
    MAX_SECONDS="$(python3 -c "
import sys, re
s = sys.argv[1]
m = re.match(r'(\d+)([smh]?)', s)
if not m: print(0); sys.exit()
n = int(m.group(1))
u = m.group(2) or 's'
print(n * {'s':1,'m':60,'h':3600}[u])
" "$DURATION" 2>/dev/null || echo 0)"
fi

METRICS_URL="${TARGET_URL}/metrics"
START_EPOCH="$(date +%s)"

while true; do
    # Stop if duration exceeded
    if [ "$MAX_SECONDS" -gt 0 ]; then
        NOW_EPOCH="$(date +%s)"
        if [ $(( NOW_EPOCH - START_EPOCH )) -ge "$MAX_SECONDS" ]; then
            break
        fi
    fi

    TS_ISO="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    # Scrape metrics, prefix each line with the timestamp.
    # curl -s -m 2: 2s timeout so a slow/hung backend doesn't stall us.
    BODY="$(curl -s -m 2 "$METRICS_URL" 2>/dev/null || true)"
    if [ -n "$BODY" ]; then
        echo "# scrape_ts: $TS_ISO" >> "$OUT_FILE"
        echo "$BODY" >> "$OUT_FILE"
        echo "" >> "$OUT_FILE"
    fi

    sleep 1
done
