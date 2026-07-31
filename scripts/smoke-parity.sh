#!/usr/bin/env bash
# smoke-parity.sh - 5-step smoke test to verify manage.sh commands work
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MANAGE="$PROJECT_DIR/manage.sh"

# Load paths.env (repo root) - single source of truth for source/build/SQL
# /config paths (Task 5 / M0, design.md review H2).
PATHS_ENV_FILE="$PROJECT_DIR/paths.env"
if [ ! -f "$PATHS_ENV_FILE" ]; then
    echo "[Error] paths.env not found at $PATHS_ENV_FILE"
    exit 1
fi
set -a
# shellcheck disable=SC1090
source "$PATHS_ENV_FILE"
set +a
COMPOSE_FILE_ABS="$PROJECT_DIR/$COMPOSE_FILE_REL"

SERVER_PID=""
RESULT=0

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "[Cleanup] Stopping server (PID $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    # Ensure docker is down (compose file was relocated to deploy/docker/)
    cd "$PROJECT_DIR"
    docker-compose -f "$COMPOSE_FILE_ABS" down 2>/dev/null || true
}
trap cleanup EXIT

echo "========================================"
echo "Smoke Parity Test (Linux/macOS)"
echo "========================================"
echo ""

# Step 1: manage build-backend
echo "[Step 1/5] manage build-backend"
bash "$MANAGE" build-backend || { echo "[FAIL] build-backend"; exit 1; }
echo "[PASS] build-backend"
echo ""

# Step 2: manage test-backend
echo "[Step 2/5] manage test-backend"
bash "$MANAGE" test-backend || { echo "[FAIL] test-backend"; exit 1; }
echo "[PASS] test-backend"
echo ""

# Step 3: manage run-backend & wait + curl /health/ready
echo "[Step 3/5] manage run-backend + health check"
bash "$MANAGE" run-backend &
SERVER_PID=$!
echo "  Server PID: $SERVER_PID"
echo "  Waiting for server startup..."
sleep 8

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[FAIL] Server process died"
    exit 1
fi

# Health check
HTTP_CODE=$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:5555/health/ready 2>/dev/null || echo "000")
if [ "$HTTP_CODE" = "200" ]; then
    echo "  /health/ready returned 200"
    echo "[PASS] run-backend + health"
else
    echo "[FAIL] /health/ready returned $HTTP_CODE"
    RESULT=1
fi
echo ""

# Step 4: Kill server
echo "[Step 4/5] Kill server"
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""
echo "[PASS] Server stopped"
echo ""

# Step 5: docker-up -> health -> docker-down
echo "[Step 5/5] docker-up -> health -> docker-down"
bash "$MANAGE" docker-up || { echo "[FAIL] docker-up"; exit 1; }
sleep 5

# Check if the docker stack has a health endpoint
DOCKER_HEALTH=$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:5555/health/ready 2>/dev/null || echo "000")
echo "  Docker health: $DOCKER_HEALTH"

bash "$MANAGE" docker-down || { echo "[FAIL] docker-down"; exit 1; }
echo "[PASS] docker-up/docker-down"
echo ""

# Summary
echo "========================================"
if [ $RESULT -eq 0 ]; then
    echo "ALL SMOKE TESTS PASSED"
else
    echo "SMOKE TESTS FAILED"
fi
echo "========================================"
exit $RESULT
