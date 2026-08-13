#!/usr/bin/env bash
# CMake/ctest wrapper for the out-of-process endpoint tests (方案 A).
#
# Starts the authforge server as a background process, waits for health
# readiness, runs the OAuth2 + Admin endpoint test scripts against it, then
# stops the server. Returns non-zero if any endpoint test failed.
#
# Registered as a single ctest entry (EndpointTests_OutOfProcess) so that
# `ctest` alone exercises the full out-of-process HTTP stack -- the tests
# formerly reachable only via full-test.sh Step 6-7.
#
# Prerequisites (NOT managed by this script -- must be satisfied by the
# environment, same as full-test.sh):
#   - PostgreSQL + Redis running
#   - Database migrated + seeded (setup-database.sh)
#   - jq installed (for JSON parsing in the endpoint test scripts)
#   - Server binary built
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVER_EXE=""
BASE_URL="http://127.0.0.1:5555"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --server-exe) SERVER_EXE="$2"; shift 2 ;;
        --base-url)   BASE_URL="$2";   shift 2 ;;
        *) shift ;;
    esac
done

if [ -z "$SERVER_EXE" ]; then
    echo "ERROR: --server-exe is required"
    exit 2
fi

RESULT=0

# Kill any stale server instance.
pkill -f authforge-server 2>/dev/null || true
sleep 1

SERVER_DIR="$(dirname "$SERVER_EXE")"
echo "[endpoint-wrapper] Starting server: $SERVER_EXE"
echo "[endpoint-wrapper] Working dir:     $SERVER_DIR"

cd "$SERVER_DIR"
"$SERVER_EXE" &
SERVER_PID=$!

cleanup() {
    echo "[endpoint-wrapper] Stopping server (PID $SERVER_PID)"
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT

# Wait for health readiness (up to 30s).
ready=false
for i in $(seq 1 30); do
    if curl -s "$BASE_URL/health/live" 2>/dev/null | grep -q '"ok"'; then
        ready=true; break
    fi
    sleep 1
done
if [ "$ready" != true ]; then
    echo "[endpoint-wrapper] ERROR: Server did not become ready within 30s"
    exit 1
fi
echo "[endpoint-wrapper] Server ready (PID $SERVER_PID)"

# Run the endpoint test scripts.
bash "$SCRIPT_DIR/test-oauth2-endpoints.sh" || RESULT=1
bash "$SCRIPT_DIR/test-admin-endpoints.sh" || RESULT=1

if [ "$RESULT" -eq 0 ]; then
    echo "[endpoint-wrapper] ALL endpoint tests PASSED"
else
    echo "[endpoint-wrapper] Some endpoint tests FAILED"
fi
exit $RESULT
