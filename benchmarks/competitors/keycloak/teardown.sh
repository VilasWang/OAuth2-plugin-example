#!/usr/bin/env bash
# benchmarks/competitors/keycloak/teardown.sh — full clean teardown (design §5.1:
# every product stack must leave zero residue before the next one boots).
set -euo pipefail

KC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# compose interpolates the whole file even for `down`; supply throwaway values
# (down creates nothing, the real secret is only needed at `up`).
export KC_BOOTSTRAP_ADMIN_USERNAME="${KC_BOOTSTRAP_ADMIN_USERNAME:-down}"
export KC_BOOTSTRAP_ADMIN_PASSWORD="${KC_BOOTSTRAP_ADMIN_PASSWORD:-down}"
docker compose -f "$KC_DIR/docker-compose.yml" -p kc-bench down -v --remove-orphans 2>/dev/null || true

# residue assertions (AC-M1.1b): no containers/volumes/networks with our prefix
LEFT_C="$(docker ps -a --format '{{.Names}}' | grep -c '^kc-bench-' || true)"
LEFT_V="$(docker volume ls --format '{{.Name}}' | grep -c '^kc-bench-' || true)"
LEFT_N="$(docker network ls --format '{{.Name}}' | grep -c '^kc-bench-' || true)"
if [ "$LEFT_C" != "0" ] || [ "$LEFT_V" != "0" ] || [ "$LEFT_N" != "0" ]; then
    echo "[teardown] ERROR: residue left behind (containers=$LEFT_C volumes=$LEFT_V networks=$LEFT_N)"
    exit 1
fi
echo "[teardown] clean (no kc-bench-* containers/volumes/networks remain)"
