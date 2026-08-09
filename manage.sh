#!/usr/bin/env bash
# manage.sh - Unified project management script for Linux/macOS
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ACTION="${1:-}"
CONFIG="Release"

# Load paths.env - single source of truth for source/build/SQL/config paths
# (Task 5 / M0, design.md review H2). Later milestones only need to update
# paths.env; this script reads the resulting variables instead of
# hardcoding directory names.
PATHS_ENV_FILE="$SCRIPT_DIR/paths.env"
if [ ! -f "$PATHS_ENV_FILE" ]; then
    echo "[Error] paths.env not found at $PATHS_ENV_FILE"
    exit 1
fi
set -a
# shellcheck disable=SC1090
source "$PATHS_ENV_FILE"
set +a
COMPOSE_FILE_ABS="$SCRIPT_DIR/$COMPOSE_FILE_REL"

# Parse global options
for arg in "$@"; do
    case "$arg" in
        -debug|--debug) CONFIG="Debug" ;;
    esac
done

show_help() {
    echo "Usage: ./manage.sh <command> [options]"
    echo ""
    echo "Commands:"
    echo "  build-backend [-debug]       Build the C++ project (Plugin + Server)"
    echo "  test-backend [-debug]        Run backend tests"
    echo "  build-frontend               Build the Vue frontend"
    echo "  dev-frontend                 Run frontend in dev mode"
    echo "  build-admin                  Build the admin frontend"
    echo "  dev-admin                    Run admin frontend in dev mode"
    echo "  run-backend [-debug]         Start the authforge-server binary"
    echo "  setup-db                     Create database and run migrations"
    echo "  generate-models              Generate Drogon ORM models"
    echo "  reset-password               Reset admin password to default"
    echo "  reset-lockout                Reset account lockout counters"
    echo "  test-admin-endpoints         Run admin API endpoint tests"
    echo "  test-oauth2-endpoints        Run OAuth2 endpoint tests"
    echo "  e2e-admin                    Full test (build + unit + admin API)"
    echo "  e2e-frontend                 Full test with Docker"
    echo "  full-test                    Full build + test + API test cycle"
    echo "  docker-up                    Start the full stack with Docker Compose"
    echo "  docker-down                  Stop the Docker Compose stack"
    echo "  clean                        Clean build artifacts"
    echo "  help                         Show this help"
}

if [ -z "$ACTION" ]; then
    show_help
    exit 0
fi

case "$ACTION" in
    build-backend)
        bash "$SCRIPT_DIR/scripts/backend/build.sh" "--${CONFIG,,}"
        ;;
    test-backend)
        bash "$SCRIPT_DIR/scripts/backend/test.sh" "--${CONFIG,,}"
        ;;
    build-frontend)
        cd "$SCRIPT_DIR/$OAUTH2_FRONTEND_DIR"
        npm install
        npm run build
        ;;
    dev-frontend)
        cd "$SCRIPT_DIR/$OAUTH2_FRONTEND_DIR"
        npm install
        npm run dev
        ;;
    build-admin)
        cd "$SCRIPT_DIR/$OAUTH2_ADMIN_DIR"
        npm install
        npm run build
        ;;
    dev-admin)
        cd "$SCRIPT_DIR/$OAUTH2_ADMIN_DIR"
        npm install
        npm run dev
        ;;
    run-backend)
        bash "$SCRIPT_DIR/scripts/backend/run-server.sh" "--${CONFIG,,}"
        ;;
    setup-db)
        bash "$SCRIPT_DIR/scripts/backend/setup-database.sh"
        ;;
    generate-models)
        bash "$SCRIPT_DIR/scripts/backend/generate-models.sh"
        ;;
    reset-password)
        bash "$SCRIPT_DIR/scripts/backend/reset-admin-password.sh"
        ;;
    reset-lockout)
        bash "$SCRIPT_DIR/scripts/backend/reset-account-lockout.sh"
        ;;
    test-admin-endpoints)
        bash "$SCRIPT_DIR/scripts/backend/test-admin-endpoints.sh"
        ;;
    test-oauth2-endpoints)
        bash "$SCRIPT_DIR/scripts/backend/test-oauth2-endpoints.sh"
        ;;
    e2e-admin)
        bash "$SCRIPT_DIR/scripts/backend/full-test.sh" "--${CONFIG,,}"
        ;;
    e2e-frontend)
        bash "$SCRIPT_DIR/scripts/backend/full-test-docker.sh" "--${CONFIG,,}"
        ;;
    full-test)
        bash "$SCRIPT_DIR/scripts/backend/full-test.sh" "--${CONFIG,,}"
        ;;
    docker-up)
        cd "$SCRIPT_DIR"
        # Workaround for compose v5.3.1 buildx-bake relative-path bug (#45):
        # layer an absolute-path override so build contexts/bind mounts resolve
        # correctly regardless of compose version. Harmless on unaffected versions.
        OVERRIDE="$(bash "$SCRIPT_DIR/scripts/docker/compose-override.sh" "$COMPOSE_FILE_ABS")"
        docker compose -f "$COMPOSE_FILE_ABS" -f "$OVERRIDE" --project-directory . up -d
        rm -f "$OVERRIDE"
        ;;
    docker-down)
        cd "$SCRIPT_DIR"
        # Override is not needed for `down` (no build/path resolution), but
        # layering it keeps the invocation uniform and avoids a divergence if
        # the compose file gains build-keyed services later.
        OVERRIDE="$(bash "$SCRIPT_DIR/scripts/docker/compose-override.sh" "$COMPOSE_FILE_ABS")"
        docker compose -f "$COMPOSE_FILE_ABS" -f "$OVERRIDE" --project-directory . down
        rm -f "$OVERRIDE"
        ;;
    clean)
        rm -rf "$SCRIPT_DIR/$BUILD_DIR"
        rm -rf "$SCRIPT_DIR/$OAUTH2_FRONTEND_DIR/dist"
        rm -rf "$SCRIPT_DIR/$OAUTH2_ADMIN_DIR/dist"
        echo "Cleaned build artifacts."
        ;;
    help)
        show_help
        ;;
    *)
        echo "Unknown command: $ACTION"
        show_help
        exit 1
        ;;
esac
