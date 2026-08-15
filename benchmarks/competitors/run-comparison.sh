#!/usr/bin/env bash
# benchmarks/competitors/run-comparison.sh — one-command four-product run (AC3).
#
# Serial execution per design §5.1: AuthForge session → Keycloak → Ory Hydra
# → Zitadel, full teardown between products (no container/volume/network
# residue), then aggregate COMPARISON.md via gen-comparison.py.
#
# Usage:
#   bash benchmarks/competitors/run-comparison.sh --fresh        # everything
#   bash benchmarks/competitors/run-comparison.sh --only keycloak # single product
#   bash benchmarks/competitors/run-comparison.sh --report-only  # regenerate COMPARISON.md
#
# ⚠️ Single machine session: don't run anything else heavy while this runs
# (design risk R7: same-session comparability).
set -euo pipefail

COMP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$COMP_DIR/../.." && pwd)"
RESULTS_DIR="$COMP_DIR/results"
mkdir -p "$RESULTS_DIR"

MODE="all"
ONLY=""
if [ "${1:-}" = "--only" ]; then
    MODE="only"; ONLY="${2:?--only needs a product: authforge|keycloak|ory|zitadel}"
elif [ "${1:-}" = "--report-only" ]; then
    MODE="report"
elif [ "${1:-}" = "--fresh" ] || [ "${1:-}" = "" ]; then
    MODE="all"
else
    echo "usage: run-comparison.sh [--fresh | --only <product> | --report-only]" >&2
    exit 2
fi

run_product() {  # <dir-name> <setup> <runall> <teardown>
    local name="$1" setup="$2" runall="$3" teardown="$4"
    echo ""
    echo "================ $name ================"
    bash "$setup"
    bash "$runall"
    bash "$teardown"
}

if [ "$MODE" = "report" ]; then
    :
elif [ "$MODE" = "only" ]; then
    case "$ONLY" in
        authforge) bash "$COMP_DIR/run-authforge-session.sh" ;;
        keycloak)  run_product keycloak "$COMP_DIR/keycloak/setup.sh" "$COMP_DIR/keycloak/run-all.sh" "$COMP_DIR/keycloak/teardown.sh" ;;
        ory)       run_product ory "$COMP_DIR/ory/setup.sh" "$COMP_DIR/ory/run-all.sh" "$COMP_DIR/ory/teardown.sh" ;;
        zitadel)   run_product zitadel "$COMP_DIR/zitadel/setup.sh" "$COMP_DIR/zitadel/run-all.sh" "$COMP_DIR/zitadel/teardown.sh" ;;
        *) echo "unknown product: $ONLY" >&2; exit 2 ;;
    esac
else
    bash "$COMP_DIR/run-authforge-session.sh"
    run_product keycloak "$COMP_DIR/keycloak/setup.sh" "$COMP_DIR/keycloak/run-all.sh" "$COMP_DIR/keycloak/teardown.sh"
    run_product ory "$COMP_DIR/ory/setup.sh" "$COMP_DIR/ory/run-all.sh" "$COMP_DIR/ory/teardown.sh"
    run_product zitadel "$COMP_DIR/zitadel/setup.sh" "$COMP_DIR/zitadel/run-all.sh" "$COMP_DIR/zitadel/teardown.sh"
fi

echo ""
echo "================ aggregating COMPARISON.md ================"
python3 "$REPO_ROOT/benchmarks/reporting/gen-comparison.py" > "$RESULTS_DIR/COMPARISON.md"
echo "[run-comparison] report: $RESULTS_DIR/COMPARISON.md"
