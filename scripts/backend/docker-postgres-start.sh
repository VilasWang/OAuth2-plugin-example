#!/usr/bin/env bash
# docker-postgres-start.sh - Start PostgreSQL in Docker (Linux/macOS)
set -euo pipefail

source "$(dirname "$0")/env_common.sh"

echo "Starting PostgreSQL in Docker..."

cd "$PROJECT_DIR"

# Stop any existing containers
echo "Stopping existing containers..."
docker-compose -f "$COMPOSE_FILE" down 2>/dev/null || true

# Start PostgreSQL
echo "Starting PostgreSQL container..."
docker-compose -f "$COMPOSE_FILE" up -d fulla-postgres

MAX_WAIT=30
WAIT_COUNT=0

echo "Waiting for PostgreSQL to be ready..."
while ! docker exec fulla-postgres pg_isready -U fulla_user -d fulla_db &>/dev/null; do
    WAIT_COUNT=$((WAIT_COUNT + 1))
    if [ $WAIT_COUNT -ge $MAX_WAIT ]; then
        echo ""
        echo "[FAILED] PostgreSQL did not become ready in ${MAX_WAIT}s"
        docker-compose -f "$COMPOSE_FILE" down
        exit 1
    fi
    echo "  Waiting... ($WAIT_COUNT/$MAX_WAIT)"
    sleep 1
done

echo ""
echo "[SUCCESS] PostgreSQL is ready!"
echo ""
echo "Connection info:"
echo "  Host: 127.0.0.1"
echo "  Port: 5432"
echo "  Database: fulla_db"
echo "  User: fulla_user"
echo "  Password: 123456"
