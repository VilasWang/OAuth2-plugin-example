#!/usr/bin/env bash
# benchmarks/authforge/teardown.sh
#
# Stop the benchmark target stack and (by default) remove volumes so the next
# setup starts from a deterministic fresh schema + seed. Part of
# benchmark-facility-design.md M1.
#
# Usage:
#   bash benchmarks/authforge/teardown.sh          # stop + remove volumes
#   KEEP_VOLUME=1 bash benchmarks/authforge/teardown.sh  # stop only, keep data
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$BENCH_DIR/../.." && pwd)"

PATHS_ENV_FILE="$REPO_ROOT/paths.env"
if [ ! -f "$PATHS_ENV_FILE" ]; then
    echo "[teardown] ERROR: paths.env not found at $PATHS_ENV_FILE"
    exit 1
fi
set -a
# shellcheck disable=SC1090
source "$PATHS_ENV_FILE"
set +a
COMPOSE_FILE_ABS="$REPO_ROOT/$COMPOSE_FILE_REL"

if [ "${KEEP_VOLUME:-0}" = "1" ]; then
    echo "[teardown] stopping stack (volumes kept, KEEP_VOLUME=1)..."
    docker compose -f "$COMPOSE_FILE_ABS" --project-directory "$REPO_ROOT" down
else
    echo "[teardown] stopping stack + removing volumes (deterministic reset)..."
    docker compose -f "$COMPOSE_FILE_ABS" --project-directory "$REPO_ROOT" down -v \
        --remove-orphans
fi
echo "[teardown] done."
