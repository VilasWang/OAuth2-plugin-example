#!/usr/bin/env bash
# setup-database.sh - Create database, run migrations and seed data (Linux/macOS)
set -euo pipefail

source "$(dirname "$0")/env_common.sh"

DB_USER="${FULLA_DB_USER:-fulla_user}"
DB_NAME="${FULLA_DB_NAME:-fulla_db}"
DB_PASSWORD="${FULLA_DB_PASSWORD:-123456}"
DB_HOST="${FULLA_DB_HOST:-localhost}"
DB_PORT="${FULLA_DB_PORT:-5432}"

MIGRATIONS_DIR="$FULLA_SERVER_ABS_DIR/$SQL_MIGRATIONS_REL_DIR"
SEED_DIR="$FULLA_SERVER_ABS_DIR/$SQL_SEED_REL_DIR"

# Check for psql
if ! command -v psql &>/dev/null; then
    echo "[Error] psql not found in PATH."
    exit 1
fi

echo "Setting up $DB_NAME database..."

export PGPASSWORD="$DB_PASSWORD"

echo "Dropping existing database..."
psql -U "$DB_USER" -h "$DB_HOST" -p "$DB_PORT" -d postgres \
    -c "DROP DATABASE IF EXISTS $DB_NAME;" 2>/dev/null || true

echo "Creating new database..."
# Do NOT swallow stderr here: a missing role, wrong password or unreachable
# server must surface loudly with actionable guidance (parity with the .bat).
if ! psql -U "$DB_USER" -h "$DB_HOST" -p "$DB_PORT" -d postgres \
    -c "CREATE DATABASE $DB_NAME;"; then
    echo "[Error] Failed to create database \"$DB_NAME\" as role \"$DB_USER\"." >&2
    echo "        Verify the role exists, FULLA_DB_PASSWORD is correct, and that" >&2
    echo "        PostgreSQL is reachable at $DB_HOST:$DB_PORT." >&2
    exit 1
fi

# Apply migrations
if [ -d "$MIGRATIONS_DIR" ]; then
    echo "Applying migrations from $MIGRATIONS_DIR..."
    for f in "$MIGRATIONS_DIR"/V*.sql; do
        [ -f "$f" ] || continue
        echo "  Applying $(basename "$f")..."
        psql -U "$DB_USER" -h "$DB_HOST" -p "$DB_PORT" -d "$DB_NAME" -f "$f"
    done
else
    echo "[Error] Migrations directory not found: $MIGRATIONS_DIR"
    exit 1
fi

# Apply seed data (explicit list: benchmark-only seeds live in
# benchmarks/fulla/seed and must never land in a dev/test database)
if [ -d "$SEED_DIR" ]; then
    echo "Applying seed data from $SEED_DIR..."
    for f in dev_admin_user.sql; do
        [ -f "$SEED_DIR/$f" ] || continue
        echo "  Applying $f..."
        psql -U "$DB_USER" -h "$DB_HOST" -p "$DB_PORT" -d "$DB_NAME" -f "$SEED_DIR/$f"
    done
fi

unset PGPASSWORD
echo "Database setup complete!"
