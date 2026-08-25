#!/usr/bin/env bash
# full-test-docker.sh - One-click build and test using Docker (Linux/macOS)
set -euo pipefail

source "$(dirname "$0")/env_common.sh"

BUILD_TYPE="Release"
BUILD_ARG="--release"

for arg in "$@"; do
    case "$arg" in
        --debug|-debug) BUILD_TYPE="Debug"; BUILD_ARG="--debug" ;;
        --release|-release) BUILD_TYPE="Release"; BUILD_ARG="--release" ;;
    esac
done

SERVER_PID=""

cleanup() {
    echo ""
    echo "Cleaning up..."
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    cd "$PROJECT_DIR"
    docker-compose -f "$COMPOSE_FILE" down 2>/dev/null || true
}
trap cleanup EXIT

echo ""
echo "========================================"
echo "One-Click Build and Test (Docker)"
echo "========================================"
echo ""

# Prerequisites check
for cmd in docker docker-compose; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "[FAILED] $cmd is not installed or not in PATH"
        exit 1
    fi
done

docker ps &>/dev/null || { echo "[FAILED] Docker is not running"; exit 1; }
echo "[OK] All prerequisites found"
echo ""

# Step 1: Start PostgreSQL in Docker
echo "========================================"
echo "Step 1: Starting PostgreSQL in Docker"
echo "========================================"
cd "$PROJECT_DIR"
docker-compose -f "$COMPOSE_FILE" down 2>/dev/null || true
docker-compose -f "$COMPOSE_FILE" up -d fulla-postgres fulla-redis

MAX_WAIT=30
WAIT_COUNT=0
echo "Waiting for PostgreSQL to be ready..."
while ! docker exec fulla-postgres pg_isready -U fulla_user -d fulla_db &>/dev/null; do
    WAIT_COUNT=$((WAIT_COUNT + 1))
    if [ $WAIT_COUNT -ge $MAX_WAIT ]; then
        echo "[FAILED] PostgreSQL did not become ready in ${MAX_WAIT}s"
        exit 1
    fi
    echo "  Waiting... ($WAIT_COUNT/$MAX_WAIT)"
    sleep 1
done
echo "[SUCCESS] PostgreSQL is ready"
echo ""

# Step 2: Reinitialize Database
echo "========================================"
echo "Step 2: Reinitializing fulla_db database"
echo "========================================"
docker exec fulla-postgres psql -U fulla_user -d postgres -c "DROP DATABASE IF EXISTS fulla_db;"
docker exec fulla-postgres psql -U fulla_user -d postgres -c "CREATE DATABASE fulla_db;"

echo "Applying migrations..."
for f in "$FULLA_SERVER_ABS_DIR/$SQL_MIGRATIONS_REL_DIR"/V*.sql; do
    [ -f "$f" ] || continue
    echo "  Applying $(basename "$f")..."
    docker exec -i fulla-postgres psql -U fulla_user -d fulla_db < "$f"
done

echo "Applying seed data..."
for f in "$FULLA_SERVER_ABS_DIR/$SQL_SEED_REL_DIR"/*.sql; do
    [ -f "$f" ] || continue
    echo "  Applying $(basename "$f")..."
    docker exec -i fulla-postgres psql -U fulla_user -d fulla_db < "$f"
done
echo "[SUCCESS] Database initialized"
echo ""

# Step 3: Regenerate ORM Models
echo "========================================"
echo "Step 3: Regenerating ORM models"
echo "========================================"
bash "$SCRIPT_DIR/generate-models.sh" -y
echo "[SUCCESS] ORM models regenerated"
echo ""

# Step 4: Rebuild Project
echo "========================================"
echo "Step 4: Rebuilding project"
echo "========================================"
bash "$SCRIPT_DIR/build.sh" "$BUILD_ARG"
echo "[SUCCESS] Project built"
echo ""

# Step 5: Run Tests
echo "========================================"
echo "Step 5: Running tests"
echo "========================================"
bash "$SCRIPT_DIR/test.sh" "$BUILD_ARG"
echo "[SUCCESS] All tests passed"
echo ""

# Step 6: Start Server
echo "========================================"
echo "Step 6: Starting OAuth2 server"
echo "========================================"
EXE_PATH="$BUILD_ABS_DIR/$SERVER_BUILD_SUBDIR/$SERVER_BINARY_NAME"
if [ ! -f "$EXE_PATH" ]; then
    EXE_PATH="$BUILD_ABS_DIR/$SERVER_BUILD_SUBDIR/$BUILD_TYPE/$SERVER_BINARY_NAME"
fi
if [ ! -f "$EXE_PATH" ]; then
    echo "[FAILED] Server executable not found"
    exit 1
fi

export FULLA_DB_HOST=127.0.0.1
export FULLA_DB_PORT=5433
export FULLA_REDIS_HOST=127.0.0.1
export FULLA_REDIS_PORT=6380
export FULLA_REDIS_PASSWORD=redis_secret_pass

cd "$(dirname "$EXE_PATH")"
"$EXE_PATH" &
SERVER_PID=$!
sleep 5

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[FAILED] Server failed to start"
    exit 1
fi
echo "[SUCCESS] Server started (PID $SERVER_PID)"
echo ""

# Step 7: Test OAuth2 Endpoints
echo "========================================"
echo "Step 7: Testing OAuth2 endpoints"
echo "========================================"
bash "$SCRIPT_DIR/test-oauth2-endpoints.sh"
echo "[SUCCESS] OAuth2 endpoint tests passed"
echo ""

# Step 8: Test Admin Endpoints
echo "========================================"
echo "Step 8: Testing Admin endpoints"
echo "========================================"
bash "$SCRIPT_DIR/test-admin-endpoints.sh"
echo "[SUCCESS] Admin endpoint tests passed"
echo ""

# Step 9: Stop Server
echo "========================================"
echo "Step 9: Stopping OAuth2 server"
echo "========================================"
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""
echo "[SUCCESS] Server stopped"
echo ""

# Step 10: Stop Docker
echo "========================================"
echo "Step 10: Stopping Docker containers"
echo "========================================"
cd "$PROJECT_DIR"
docker-compose -f "$COMPOSE_FILE" down
echo "[SUCCESS] Docker containers stopped"
echo ""

echo "========================================"
echo "ALL STEPS COMPLETED SUCCESSFULLY!"
echo "========================================"
