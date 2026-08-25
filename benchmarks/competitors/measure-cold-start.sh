#!/usr/bin/env bash
# benchmarks/competitors/measure-cold-start.sh — generic competitor cold start.
#
# Two modes (aligned with the Fulla facility's measure-cold-start.sh):
#   * fresh   — volumes wiped, `compose up -d`, measure until the ready probe
#               answers 200. Includes the product's automatic DB schema
#               creation (the heavy part of a truly cold boot). Realm/client
#               setup is configuration, not boot, and is NOT included.
#   * restart — volumes kept, service container restarted; measures the
#               warm-boot path only.
#
# Usage:
#   bash measure-cold-start.sh --product keycloak --product-version 26.7.1 \
#       --compose-dir .../keycloak --project kc-bench --service keycloak \
#       --target http://127.0.0.1:8080 --ready-path /health/ready \
#       --out-dir benchmarks/competitors/results
#
# Options: --runs 1 (repetitions per mode; JSON stores an array)
#
# Fresh-mode note: Ory Hydra's documented boot sequence requires an explicit
# `migrate sql` step before serve (Keycloak and Zitadel create their schema
# automatically on first start); this script runs it for --product ory.
#
# The Keycloak compose file requires KC_BOOTSTRAP_ADMIN_* env vars (nothing
# committed); this script generates throwaway values for `up` invocations.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

PRODUCT="" PRODUCT_VERSION="" COMPOSE_DIR="" PROJECT="" SERVICE=""
TARGET="" READY_PATH="/health/ready" OUT_DIR="" RUNS=1
while [ "$#" -gt 0 ]; do
    case "$1" in
        --product) PRODUCT="$2"; shift 2 ;;
        --product-version) PRODUCT_VERSION="$2"; shift 2 ;;
        --compose-dir) COMPOSE_DIR="$2"; shift 2 ;;
        --project) PROJECT="$2"; shift 2 ;;
        --service) SERVICE="$2"; shift 2 ;;
        --target) TARGET="$2"; shift 2 ;;
        --ready-path) READY_PATH="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --runs) RUNS="$2"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done
for req in PRODUCT COMPOSE_DIR PROJECT SERVICE TARGET OUT_DIR; do
    [ -n "${!req}" ] || { echo "missing --$(echo "$req" | tr 'A-Z' 'a-z' | tr _ -)" >&2; exit 2; }
done

# All three competitor composes have REQUIRED env vars (nothing committed);
# supply throwaway values for `up`/`restart` invocations (fresh mode wipes
# volumes anyway; restart never re-reads these).
export KC_BOOTSTRAP_ADMIN_USERNAME="${KC_BOOTSTRAP_ADMIN_USERNAME:-admin}"
export KC_BOOTSTRAP_ADMIN_PASSWORD="${KC_BOOTSTRAP_ADMIN_PASSWORD:-$(head -c 18 /dev/urandom | base64 | tr -d '/+=' | head -c 20)}"
export HYDRA_SECRETS_SYSTEM="${HYDRA_SECRETS_SYSTEM:-$(head -c 32 /dev/urandom | base64 | tr -d '/+=' | head -c 32)}"
export ZITADEL_MASTERKEY="${ZITADEL_MASTERKEY:-$(head -c 32 /dev/urandom | base64 | tr -d '/+=' | head -c 32)}"

COMPOSE=(docker compose -f "$COMPOSE_DIR/docker-compose.yml" -p "$PROJECT")

probe_until_ready() {  # <t0-epoch> -> seconds (float)
    local t0="$1" t1 code
    while true; do
        code="$(curl -sk -o /dev/null -w '%{http_code}' --max-time 2 "$TARGET$READY_PATH" 2>/dev/null || echo 000)"
        [ "$code" = "200" ] && break
        t1="$(date +%s.%N)"
        if python3 -c "import sys; sys.exit(0 if $t1 - $t0 > 900 else 1)"; then
            echo "[coldstart] ERROR: not ready after 900s (last code=$code)" >&2
            return 1
        fi
        sleep 0.2
    done
    t1="$(date +%s.%N)"
    python3 -c "print(round($t1 - $t0, 2))"
}

rss_of_service() {  # -> MiB (docker stats single sample)
    docker stats --no-stream --format '{{.Name}}\t{{.MemUsage}}' 2>/dev/null \
        | awk -v svc="^${PROJECT}-${SERVICE}" '$1 ~ svc {print $2}' | head -1 \
        | grep -oE '[0-9.]+[GM]iB' | head -1 || echo "n/a"
}

measure_mode() {  # <fresh|restart> — progress to stderr, ONLY the JSON to stdout
    local mode="$1" runs_json="[]" t0 t rss
    for r in $(seq 1 "$RUNS"); do
        echo "[coldstart] mode=$mode run=$r/$RUNS..." >&2
        # timing starts at the boot COMMAND (up/restart), matching the
        # Fulla facility's measure-cold-start.sh semantics; compose's
        # dependency waits (postgres healthy) and, for ory, the documented
        # migrate step are part of the boot window. Cleanup (down -v) is not.
        if [ "$mode" = "fresh" ]; then
            "${COMPOSE[@]}" down -v --remove-orphans >/dev/null 2>&1 || true
            t0="$(date +%s.%N)"
            if [ "$PRODUCT" = "ory" ]; then
                # fixed argv (no shell string): Hydra's documented sequence
                # is migrate-before-serve — see header note
                "${COMPOSE[@]}" run --rm "$SERVICE" migrate sql -e -y \
                    --config /etc/hydra/hydra.yml >/dev/null 2>&1 \
                    || { echo "[coldstart] ERROR: hydra migrate failed" >&2; return 1; }
            fi
            "${COMPOSE[@]}" up -d >/dev/null 2>&1
        else
            t0="$(date +%s.%N)"
            docker restart "${PROJECT}-${SERVICE}" >/dev/null 2>&1
        fi
        t="$(probe_until_ready "$t0")"
        rss="$(rss_of_service)"
        echo "  ready in ${t}s (RSS at ready: ${rss})" >&2
        runs_json="$(python3 -c "import json; a=json.loads('''$runs_json'''); a.append({'run': $r, 'seconds': $t, 'rss_at_ready': '$rss'}); print(json.dumps(a))")"
    done
    MODE="$mode" RUNS_JSON="$runs_json" python3 - <<'PY'
import json, os
print(json.dumps({"mode": os.environ["MODE"], "runs": json.loads(os.environ["RUNS_JSON"])}))
PY
}

FRESH_JSON="$(measure_mode fresh)"
echo "[coldstart] fresh: $FRESH_JSON"
RESTART_JSON="$(measure_mode restart)"
echo "[coldstart] restart: $RESTART_JSON"

# leave the stack in the state run-all.sh expects (fresh boot lost realm/client
# config only if volumes were wiped — callers re-run setup.sh if needed)
mkdir -p "$OUT_DIR"
GIT_SHA="$(cd "$REPO_ROOT" && git rev-parse --short HEAD 2>/dev/null || echo nogit)"
DATE_TAG="$(date -u +%Y%m%d)"
OUT_FILE="$OUT_DIR/${DATE_TAG}-${GIT_SHA}-${PRODUCT}-coldstart.json"
FRESH="$FRESH_JSON" RESTART="$RESTART_JSON" python3 - "$OUT_FILE" "$PRODUCT" "$PRODUCT_VERSION" "$GIT_SHA" <<'PY'
import json, sys
doc = {
    "schema_version": 1,
    "kind": "coldstart",
    "product": sys.argv[2],
    "product_version": sys.argv[3],
    "fresh": json.loads(__import__("os").environ["FRESH"]),
    "restart": json.loads(__import__("os").environ["RESTART"]),
    "env": {"git_sha": sys.argv[4], "network": "localhost-cross-container"},
}
json.dump(doc, open(sys.argv[1], "w"), indent=2)
print(f"[coldstart] wrote {sys.argv[1]}")
PY
