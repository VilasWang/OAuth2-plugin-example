#!/usr/bin/env bash
# observe/docker-stats.sh — sample container CPU/RSS during a benchmark run.
#
# Part of benchmark-facility-design.md M3 (§5.4 resource observation).
# Writes TSV (timestamp, container, cpu%, mem_usage, mem_limit, net_io) at 1s
# intervals until killed or the timeout expires.
#
# Usage (typically invoked by run-scenario.sh --observe):
#   bash observe/docker-stats.sh <output.tsv> [duration]
#
# Arguments:
#   output.tsv  — output file path
#   duration    — stop after this long (e.g. "30s", "60s"); default: run until killed
#
# Env overrides:
#   CONTAINER_GLOB — case-pattern selecting which containers to record.
#                    Default: *oauth2-backend*|*oauth2-postgres*|*oauth2-redis*
#                    (competitor runs pass their own container names)

set -euo pipefail

OUT_FILE="${1:-}"
DURATION="${2:-}"

if [ -z "$OUT_FILE" ]; then
    echo "Usage: bash observe/docker-stats.sh <output.tsv> [duration]"
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

# Header
echo -e "timestamp_iso\ttimestamp_epoch\tcontainer\tcpu_pct\tmem_usage\tmem_limit\tnet_io\tblock_io" > "$OUT_FILE"

# Container-name filter: which containers count as "benchmark-relevant".
# Default targets the AuthForge stack; competitor runs override with
# CONTAINER_GLOB (a case-pattern, e.g. 'bench-keycloak*|bench-postgres*').
CONTAINER_GLOB="${CONTAINER_GLOB:-*oauth2-backend*|*oauth2-postgres*|*oauth2-redis*}"

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
    TS_EPOCH="$(date +%s)"

    # docker stats with --no-stream (single sample, then exit).
    # --format produces one line per container with tab-separated fields.
    # We target the benchmark-relevant containers.
    docker stats --no-stream \
        --format "{{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.MemPerc}}\t{{.NetIO}}\t{{.BlockIO}}" \
        2>/dev/null | while IFS=$'\t' read -r name cpu mem_usage mem_pct net_io block_io; do
            # Only record the benchmark-relevant containers
            case "$name" in
                $CONTAINER_GLOB)
                    echo -e "${TS_ISO}\t${TS_EPOCH}\t${name}\t${cpu}\t${mem_usage}\t-\t${net_io}\t${block_io}" >> "$OUT_FILE"
                    ;;
            esac
        done

    sleep 1
done
