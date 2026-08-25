#!/usr/bin/env bash
# test.sh - Run backend tests (Linux/macOS equivalent of test.bat)
set -euo pipefail

source "$(dirname "$0")/env_common.sh"

BUILD_TYPE="Release"
# Default to minimal output: only failed tests print their log, plus the
# final pass/fail/total summary line. Pass -q/--quiet to suppress even the
# per-test START/PASS lines (-Q = summary only).
VERBOSE="--output-on-failure"

for arg in "$@"; do
    case "$arg" in
        --debug|-debug) BUILD_TYPE="Debug" ;;
        --release|-release) BUILD_TYPE="Release" ;;
        -q|--quiet) VERBOSE="-Q" ;;
    esac
done

echo "========================================"
echo "Running OAuth2 Tests"
echo "========================================"
echo "Build Type: $BUILD_TYPE"

# All builds go through Conan + `cmake --preset`, each installing into its own
# build/<preset> dir (see CMakePresets.json binaryDir); derive it the same way
# build.sh does so ctest runs against the matching tree.
PRESET="$(resolve_cmake_preset "$BUILD_TYPE")"
if [ -z "$PRESET" ]; then
    echo "[Error] Could not resolve a CMake preset for this platform."
    exit 1
fi
PRESET_DIR="$BUILD_ABS_DIR/$PRESET"

if [ ! -d "$PRESET_DIR" ]; then
    echo "[Error] Build directory not found: $PRESET_DIR. Please run build.sh first."
    exit 1
fi

cd "$PRESET_DIR"

# Run 1: Standard config.json
echo ""
echo "[1/2] Running tests with standard $CONFIG_FILE..."
ctest --build-config "$BUILD_TYPE" $VERBOSE
echo "[PASS] Standard config tests successful."

# Run 2: config.ci.json
echo ""
echo "[2/2] Running tests with $CONFIG_CI_FILE..."
CI_CONFIG="$FULLA_SERVER_ABS_DIR/$CONFIG_CI_FILE"
# build.sh stages the tests' runtime config as a flat config.json under
# <preset>/tests (single-config Unix generators); fall back to a per-config
# subdir for multi-config generators.
TEST_WORK_DIR="$PRESET_DIR/$TESTS_BUILD_SUBDIR"
if [ ! -f "$TEST_WORK_DIR/$CONFIG_FILE" ] && [ -f "$TEST_WORK_DIR/$BUILD_TYPE/$CONFIG_FILE" ]; then
    TEST_WORK_DIR="$TEST_WORK_DIR/$BUILD_TYPE"
fi

if [ ! -f "$CI_CONFIG" ]; then
    echo "[SKIP] $CONFIG_CI_FILE not found, skipping second run."
    exit 0
fi

if [ -d "$TEST_WORK_DIR" ]; then
    cp "$TEST_WORK_DIR/$CONFIG_FILE" "$TEST_WORK_DIR/$CONFIG_FILE.bak"
    cp "$CI_CONFIG" "$TEST_WORK_DIR/$CONFIG_FILE"

    CI_EXIT=0
    ctest --build-config "$BUILD_TYPE" $VERBOSE || CI_EXIT=$?

    # Restore original config
    mv "$TEST_WORK_DIR/$CONFIG_FILE.bak" "$TEST_WORK_DIR/$CONFIG_FILE"

    if [ $CI_EXIT -ne 0 ]; then
        echo "[FAIL] Tests failed with $CONFIG_CI_FILE"
        exit 1
    fi
    echo "[PASS] CI config tests successful."
fi

echo ""
echo "========================================"
echo "All test runs completed successfully"
echo "========================================"
