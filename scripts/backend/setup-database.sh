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
if ! psql -U "$DB_USER" -h "$DB_HOST" -p "$DB_PORT" -d postgres     -c "CREATE DATABASE $DB_NAME;"; then
    # Common cause: the role lacks CREATEDB (DROP succeeds -- it only needs
    # ownership -- while CREATE needs the attribute). Local Linux/WSL
    # PostgreSQL usually exposes the 'postgres' superuser over peer auth,
    # so try to self-heal the attribute once and retry; otherwise fall
    # through to precise manual guidance.
    echo "[Warn] CREATE DATABASE failed -- checking whether \"$DB_USER\" lacks CREATEDB..."
    HAS_DB=$(psql -U "$DB_USER" -h "$DB_HOST" -p "$DB_PORT" -d postgres -tAc         "SELECT 1 FROM pg_roles WHERE rolname = '$DB_USER' AND rolcreatedb;" 2>/dev/null || true)
    CREATED_OK=0
    if [ "$HAS_DB" != "1" ] && command -v sudo >/dev/null 2>&1 &&        sudo -n -u postgres psql -tAc            "SELECT 1 FROM pg_roles WHERE rolname = '$DB_USER';" 2>/dev/null | grep -q 1; then
        echo "        Granting CREATEDB to \"$DB_USER\" via the local postgres superuser..."
        if sudo -n -u postgres psql -c "ALTER ROLE \"$DB_USER\" CREATEDB;" >/dev/null 2>&1; then
            if psql -U "$DB_USER" -h "$DB_HOST" -p "$DB_PORT" -d postgres                 -c "CREATE DATABASE $DB_NAME;"; then
                echo "        Self-healed: role granted CREATEDB, database created."
                CREATED_OK=1
            fi
        fi
    fi
    if [ "$CREATED_OK" != "1" ]; then
        echo "[Error] Failed to create database \"$DB_NAME\" as role \"$DB_USER\"." >&2
        echo "        If the role lacks CREATEDB (typical for a hand-created role), fix once with:" >&2
        echo "          sudo -u postgres psql -c 'ALTER ROLE $DB_USER CREATEDB;'" >&2
        echo "        (docker: docker exec <postgres-container> psql -U postgres -c 'ALTER ROLE $DB_USER CREATEDB;')" >&2
        echo "        Also verify the role exists, FULLA_DB_PASSWORD is correct, and PostgreSQL" >&2
        echo "        is reachable at $DB_HOST:$DB_PORT." >&2
        exit 1
    fi
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
    for f in dev_admin_user.sql dev_admin_console_client.sql dev_backend_client.sql dev_vue_client.sql; do
        [ -f "$SEED_DIR/$f" ] || continue
        echo "  Applying $f..."
        psql -U "$DB_USER" -h "$DB_HOST" -p "$DB_PORT" -d "$DB_NAME" -f "$SEED_DIR/$f"
    done
fi

unset PGPASSWORD
echo "Database setup complete!"
