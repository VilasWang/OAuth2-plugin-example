#!/usr/bin/env bash
# setup-database.sh - Create database, run migrations and seed data (Linux/macOS)
set -euo pipefail

source "$(dirname "$0")/env_common.sh"

DB_USER="${OAUTH2_DB_USER:-oauth2_user}"
DB_NAME="${OAUTH2_DB_NAME:-oauth2_db}"
DB_PASSWORD="${OAUTH2_DB_PASSWORD:-123456}"
DB_HOST="${OAUTH2_DB_HOST:-localhost}"
DB_PORT="${OAUTH2_DB_PORT:-5432}"

MIGRATIONS_DIR="$OAUTH2_SERVER_ABS_DIR/$SQL_MIGRATIONS_REL_DIR"
SEED_DIR="$OAUTH2_SERVER_ABS_DIR/$SQL_SEED_REL_DIR"
LEGACY_SQL_DIR="$OAUTH2_SERVER_ABS_DIR/$SQL_DIR"

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
psql -U "$DB_USER" -h "$DB_HOST" -p "$DB_PORT" -d postgres \
    -c "CREATE DATABASE $DB_NAME;" 2>/dev/null

# Apply migrations (new structure)
if [ -d "$MIGRATIONS_DIR" ]; then
    echo "Applying migrations from $MIGRATIONS_DIR..."
    for f in "$MIGRATIONS_DIR"/V*.sql; do
        [ -f "$f" ] || continue
        echo "  Applying $(basename "$f")..."
        psql -U "$DB_USER" -h "$DB_HOST" -p "$DB_PORT" -d "$DB_NAME" -f "$f"
    done
else
    # Fallback to legacy flat structure
    echo "Applying SQL schemas from $LEGACY_SQL_DIR..."
    for f in "$LEGACY_SQL_DIR"/*.sql; do
        [ -f "$f" ] || continue
        echo "  Applying $(basename "$f")..."
        psql -U "$DB_USER" -h "$DB_HOST" -p "$DB_PORT" -d "$DB_NAME" -f "$f"
    done
fi

# Apply seed data
if [ -d "$SEED_DIR" ]; then
    echo "Applying seed data from $SEED_DIR..."
    for f in "$SEED_DIR"/*.sql; do
        [ -f "$f" ] || continue
        echo "  Applying $(basename "$f")..."
        psql -U "$DB_USER" -h "$DB_HOST" -p "$DB_PORT" -d "$DB_NAME" -f "$f"
    done
fi

unset PGPASSWORD
echo "Database setup complete!"
