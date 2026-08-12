#!/usr/bin/env bash
# measure-cold-start.sh — measure backend cold-start time + peak RSS.
#
# Part of benchmark-facility-design.md M3 (§5.5 cold-start test).
# This is a standalone script, not invoked by run-scenario.sh.
#
# Two modes per the design doc:
#   Mode A (auto-migrate): OAUTH2_AUTO_MIGRATE=true → cold start INCLUDES
#          schema migration time (realistic for a cold Docker volume).
#   Mode B (pre-migrated): --migrate-only pre-run → cold start EXCLUDES
#          migration time (fair framework-only comparison).
#
# Records: cold-start seconds (up to /health/ready = 200) + RSS peak.
#
# Usage:
#   bash benchmarks/authforge/measure-cold-start.sh [--pre-migrated]
#
# Output: writes a JSON result to benchmarks/results/<date>-cold-start.json
#         and prints a one-line summary.

set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$BENCH_DIR/../.." && pwd)"
TARGET_URL="${TARGET_URL:-http://127.0.0.1:5555}"

# --- source paths.env ---
PATHS_ENV_FILE="$REPO_ROOT/paths.env"
if [ -f "$PATHS_ENV_FILE" ]; then
    set -a
    # shellcheck disable=SC1090
    source "$PATHS_ENV_FILE"
    set +a
fi

PRE_MIGRATED=0
if [ "${1:-}" = "--pre-migrated" ]; then
    PRE_MIGRATED=1
fi

cd "$REPO_ROOT"

# --- compose override (same as setup.sh) ---
COMPOSE_FILE_ABS="$REPO_ROOT/${COMPOSE_FILE_REL:-deploy/docker/docker-compose.yml}"
OVERRIDE_FILE="$(bash "$REPO_ROOT/scripts/docker/compose-override.sh" "$COMPOSE_FILE_ABS" 2>/dev/null || true)"
COMPOSE_ARGS=(-f "$COMPOSE_FILE_ABS")
[ -n "$OVERRIDE_FILE" ] && [ -f "$OVERRIDE_FILE" ] && COMPOSE_ARGS+=(-f "$OVERRIDE_FILE")
cleanup() { rm -f "$OVERRIDE_FILE"; }
trap cleanup EXIT

echo "[cold-start] mode: $([ "$PRE_MIGRATED" = "1" ] && echo "pre-migrated (--migrate-only)" || echo "auto-migrate (OAUTH2_AUTO_MIGRATE=true)")"

# --- ensure DB is up (postgres + redis) but backend is NOT running ---
echo "[cold-start] stopping backend (if running)..."
docker compose "${COMPOSE_ARGS[@]}" --project-directory "$REPO_ROOT" stop oauth2-backend >/dev/null 2>&1 || true

# --- for pre-migrated mode: run migrations first via --migrate-only ---
if [ "$PRE_MIGRATED" = "1" ]; then
    echo "[cold-start] pre-running migrations (--migrate-only)..."
    docker compose "${COMPOSE_ARGS[@]}" --project-directory "$REPO_ROOT" run --rm \
        oauth2-backend --migrate-only >/dev/null 2>&1 || echo "[cold-start] WARN: --migrate-only failed (may need OAUTH2_AUTO_MIGRATE=false)"
fi

# --- measure cold start ---
echo "[cold-start] starting backend + timer..."
START_EPOCH=$(date +%s)
START_NS=$(date +%s%N)

docker compose "${COMPOSE_ARGS[@]}" --project-directory "$REPO_ROOT" up -d oauth2-backend >/dev/null 2>&1

# Poll /health/ready until 200
READY=0
for i in $(seq 1 120); do
    CODE="$(curl -s -o /dev/null -w '%{http_code}' "$TARGET_URL/health/ready" 2>/dev/null || echo 000)"
    if [ "$CODE" = "200" ]; then
        READY=1
        END_NS=$(date +%s%N)
        COLD_START_NS=$(( END_NS - START_NS ))
        COLD_START_S=$(python3 -c "print(f'{$COLD_START_NS / 1e9:.3f}')")
        echo "[cold-start] ready after ${COLD_START_S}s ($i polls)"
        break
    fi
    sleep 0.5
done

if [ "$READY" != "1" ]; then
    echo "[cold-start] ERROR: backend did not become ready within 60s"
    exit 1
fi

# --- sample RSS peak (best-effort: docker stats for ~2s) ---
echo "[cold-start] sampling RSS..."
RSS_BYTES=0
for i in $(seq 1 4); do
    MEM="$(docker stats --no-stream --format "{{.MemUsage}}" $(docker compose "${COMPOSE_ARGS[@]}" --project-directory "$REPO_ROOT" ps -q oauth2-backend 2>/dev/null) 2>/dev/null | head -1 || true)"
    # MemUsage format: "12.34MiB / 8GiB" — extract the first number
    if [ -n "$MEM" ]; then
        RSS_BYTES="$(python3 -c "
import re, sys
s = sys.argv[1]
m = re.match(r'([\d.]+)([A-Za-z]+)', s.strip().split('/')[0].strip())
if not m: sys.exit()
n = float(m.group(1))
u = m.group(2)
mult = {'B':1,'KiB':1024,'MiB':1048576,'GiB':1073741824,'kB':1000,'MB':1000000,'GB':1000000000}.get(u,1)
print(int(n*mult))
" "$MEM" 2>/dev/null || echo 0)"
        [ "$RSS_BYTES" -gt 0 ] && break
    fi
    sleep 0.5
done

RSS_MB="$(python3 -c "print(f'{$RSS_BYTES / 1048576:.1f}')")"

# --- write result JSON ---
DATE_TAG="$(date -u +%Y%m%d)"
GIT_SHA="$(git rev-parse --short HEAD 2>/dev/null || echo nogit)"
OUT_FILE="$REPO_ROOT/benchmarks/results/${DATE_TAG}-cold-start.json"
mkdir -p "$(dirname "$OUT_FILE")"

MODE_TAG=$([ "$PRE_MIGRATED" = "1" ] && echo "pre-migrated" || echo "auto-migrate")

python3 -c "
import json, sys
result = {
    'schema_version': 1,
    'test': 'cold-start',
    'mode': '$MODE_TAG',
    'cold_start_seconds': float('$COLD_START_S'),
    'rss_mb': float('$RSS_MB'),
    'env': {
        'git_sha': '$GIT_SHA',
        'date': '$(date -u +%Y-%m-%dT%H:%M:%SZ)',
    },
}
with open(sys.argv[1], 'w') as f:
    json.dump(result, f, indent=2)
    f.write('\n')
" "$OUT_FILE"

echo ""
echo "[cold-start] result → $OUT_FILE"
echo "[cold-start] cold_start=${COLD_START_S}s  rss=${RSS_MB}MB  mode=${MODE_TAG}"
echo ""
echo "[cold-start] done. (backend is running; stop with: docker compose down)"
