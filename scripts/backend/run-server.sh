#!/usr/bin/env bash
# run-server.sh - Start the authforge-server binary (Linux/macOS)
set -euo pipefail

source "$(dirname "$0")/env_common.sh"

BUILD_TYPE="Release"

for arg in "$@"; do
    case "$arg" in
        --debug|-debug) BUILD_TYPE="Debug" ;;
        --release|-release) BUILD_TYPE="Release" ;;
    esac
done

# All builds go through Conan + `cmake --preset`; derive the matching
# build/<preset> dir the same way build.sh does.
PRESET="$(resolve_cmake_preset "$BUILD_TYPE")"
if [ -z "$PRESET" ]; then
    echo "[Error] Could not resolve a CMake preset for this platform."
    exit 1
fi
PRESET_DIR="$BUILD_ABS_DIR/$PRESET"

# On Linux, single-config generators put the binary directly in the build dir
EXE_PATH="$PRESET_DIR/$SERVER_BUILD_SUBDIR/$SERVER_BINARY_NAME"

# Fallback: multi-config layout
if [ ! -f "$EXE_PATH" ]; then
    EXE_PATH="$PRESET_DIR/$SERVER_BUILD_SUBDIR/$BUILD_TYPE/$SERVER_BINARY_NAME"
fi

if [ ! -f "$EXE_PATH" ]; then
    echo "[Error] $SERVER_BINARY_NAME binary not found."
    echo "Searched:"
    echo "  $PRESET_DIR/$SERVER_BUILD_SUBDIR/$SERVER_BINARY_NAME"
    echo "  $PRESET_DIR/$SERVER_BUILD_SUBDIR/$BUILD_TYPE/$SERVER_BINARY_NAME"
    echo "Please run build.sh first."
    exit 1
fi

echo "Starting $SERVER_BINARY_NAME ($BUILD_TYPE)"
cd "$(dirname "$EXE_PATH")"
exec "$EXE_PATH"
