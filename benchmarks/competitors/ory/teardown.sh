#!/usr/bin/env bash
# benchmarks/competitors/ory/teardown.sh — full clean teardown (design §5.1).
set -euo pipefail

ORY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export HYDRA_SECRETS_SYSTEM="${HYDRA_SECRETS_SYSTEM:-down}"
docker compose -f "$ORY_DIR/docker-compose.yml" -p ory-bench down -v --remove-orphans 2>/dev/null || true

LEFT_C="$(docker ps -a --format '{{.Names}}' | grep -c '^ory-bench-' || true)"
LEFT_V="$(docker volume ls --format '{{.Name}}' | grep -c '^ory-bench-' || true)"
LEFT_N="$(docker network ls --format '{{.Name}}' | grep -c '^ory-bench-' || true)"
if [ "$LEFT_C" != "0" ] || [ "$LEFT_V" != "0" ] || [ "$LEFT_N" != "0" ]; then
    echo "[teardown] ERROR: residue left behind (containers=$LEFT_C volumes=$LEFT_V networks=$LEFT_N)"
    exit 1
fi
echo "[teardown] clean (no ory-bench-* containers/volumes/networks remain)"
