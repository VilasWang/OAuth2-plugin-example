#!/bin/bash
# env_common.sh - Common environment variables for Linux backend scripts

# Determine script and project directories
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_DIR="$( dirname "$( dirname "$SCRIPT_DIR" )" )"

export PROJECT_DIR
export SCRIPT_DIR

# Load paths.env (repo root) - single source of truth for source/build/SQL
# /config paths (Task 5 / M0, design.md review H2). All scripts must read
# path values via the variables below instead of hardcoding directory
# names, so later milestones (M2a/M3/M8) only need to update paths.env.
PATHS_ENV_FILE="$PROJECT_DIR/paths.env"
if [ ! -f "$PATHS_ENV_FILE" ]; then
    echo "[Error] paths.env not found at $PATHS_ENV_FILE"
    exit 1
fi
set -a
# shellcheck disable=SC1090
source "$PATHS_ENV_FILE"
set +a

# Validation (uses the plugin dir defined in paths.env)
if [ ! -d "$PROJECT_DIR/$OAUTH2_PLUGIN_DIR" ]; then
    echo "[Error] Project structure invalid. Could not find $OAUTH2_PLUGIN_DIR at $PROJECT_DIR"
    exit 1
fi

# Derived absolute paths, built from paths.env values.
OAUTH2_SERVER_ABS_DIR="$PROJECT_DIR/$OAUTH2_SERVER_DIR"
OAUTH2_PLUGIN_ABS_DIR="$PROJECT_DIR/$OAUTH2_PLUGIN_DIR"
BUILD_ABS_DIR="$PROJECT_DIR/$BUILD_DIR"
export OAUTH2_SERVER_ABS_DIR
export OAUTH2_PLUGIN_ABS_DIR
export BUILD_ABS_DIR

# Relocated Docker assets (repo-structure-refactor moved these out of the root
# into deploy/docker/). Scripts must reference them via these variables instead
# of bare `docker-compose` / `-f Dockerfile`, which assumed root-level files.
COMPOSE_FILE="$PROJECT_DIR/$COMPOSE_FILE_REL"
DOCKERFILE="$PROJECT_DIR/$DOCKERFILE_REL"
export COMPOSE_FILE
export DOCKERFILE
