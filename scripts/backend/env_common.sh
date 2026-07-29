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

# Validation (uses the server dir defined in paths.env; the former
# OAUTH2_PLUGIN_DIR check died with the OAuth2Plugin/ directory in Phase 4)
if [ ! -d "$PROJECT_DIR/$OAUTH2_SERVER_DIR" ]; then
    echo "[Error] Project structure invalid. Could not find $OAUTH2_SERVER_DIR at $PROJECT_DIR"
    exit 1
fi

# Derived absolute paths, built from paths.env values.
OAUTH2_SERVER_ABS_DIR="$PROJECT_DIR/$OAUTH2_SERVER_DIR"
LIBS_STORAGE_POSTGRES_ABS_DIR="$PROJECT_DIR/$LIBS_STORAGE_POSTGRES_DIR"
BUILD_ABS_DIR="$PROJECT_DIR/$BUILD_DIR"
export OAUTH2_SERVER_ABS_DIR
export LIBS_STORAGE_POSTGRES_ABS_DIR
export BUILD_ABS_DIR

# Resolve the CMakePresets.json preset name for the current OS + build type
# + sanitizer. All builds go through Conan + `cmake --preset`, and each
# preset installs to its own build/<preset-name> directory (binaryDir), so
# downstream scripts derive the actual output dir as
# "$BUILD_ABS_DIR/$(resolve_cmake_preset ...)".
#   Usage: PRESET=$(resolve_cmake_preset "$BUILD_TYPE" "$SANITIZER")
# SANITIZER is optional (off|address|thread); defaults to off.
resolve_cmake_preset() {
    local build_type="${1:-Release}"
    local sanitizer="${2:-off}"
    local base
    if [[ "$OSTYPE" == "darwin"* ]]; then
        base="macos-arm64"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        base="linux-release"
    else
        echo "[Error] Unsupported OSTYPE for preset resolution: $OSTYPE" >&2
        return 1
    fi
    case "$sanitizer" in
        address) echo "${base}-asan"; return 0 ;;
        thread)  echo "${base}-tsan"; return 0 ;;
    esac
    if [ "$build_type" = "Debug" ]; then
        # linux-release -> linux-debug ; macos-arm64 -> macos-arm64-debug
        if [ "$base" = "linux-release" ]; then
            echo "linux-debug"
        else
            echo "${base}-debug"
        fi
    else
        echo "$base"
    fi
}
export -f resolve_cmake_preset

# Relocated Docker assets (repo-structure-refactor moved these out of the root
# into deploy/docker/). Scripts must reference them via these variables instead
# of bare `docker-compose` / `-f Dockerfile`, which assumed root-level files.
COMPOSE_FILE="$PROJECT_DIR/$COMPOSE_FILE_REL"
DOCKERFILE="$PROJECT_DIR/$DOCKERFILE_REL"
export COMPOSE_FILE
export DOCKERFILE
