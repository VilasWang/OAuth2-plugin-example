#!/usr/bin/env bash
# benchmarks/competitors/zitadel/teardown.sh — full clean teardown (design §5.1).
set -euo pipefail

ZA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export ZITADEL_MASTERKEY="${ZITADEL_MASTERKEY:-down}"
docker compose -f "$ZA_DIR/docker-compose.yml" -p zitadel-bench down -v --remove-orphans 2>/dev/null || true
rm -f "$ZA_DIR/setup-steps.rendered.yaml"

LEFT_C="$(docker ps -a --format '{{.Names}}' | grep -c '^zitadel-bench-' || true)"
LEFT_V="$(docker volume ls --format '{{.Name}}' | grep -c '^zitadel-bench-' || true)"
LEFT_N="$(docker network ls --format '{{.Name}}' | grep -c '^zitadel-bench-' || true)"
if [ "$LEFT_C" != "0" ] || [ "$LEFT_V" != "0" ] || [ "$LEFT_N" != "0" ]; then
    echo "[teardown] ERROR: residue left behind (containers=$LEFT_C volumes=$LEFT_V networks=$LEFT_N)"
    exit 1
fi
echo "[teardown] clean (no zitadel-bench-* containers/volumes/networks remain)"
