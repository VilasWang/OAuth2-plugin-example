#!/usr/bin/env bash
# test.sh - Run backend tests (Linux/macOS equivalent of test.bat)
set -euo pipefail

source "$(dirname "$0")/env_common.sh"

BUILD_TYPE="Release"
VERBOSE="--output-on-failure"

for arg in "$@"; do
    case "$arg" in
        --debug|-debug) BUILD_TYPE="Debug" ;;
        --release|-release) BUILD_TYPE="Release" ;;
        -q|--quiet) VERBOSE="" ;;
    esac
done

echo "========================================"
echo "Running OAuth2 Tests"
echo "========================================"
echo "Build Type: $BUILD_TYPE"

if [ ! -d "$BUILD_ABS_DIR" ]; then
    echo "[Error] Build directory not found. Please run build.sh first."
    exit 1
fi

cd "$BUILD_ABS_DIR"

# Run 1: Standard config.json
echo ""
echo "[1/2] Running tests with standard $CONFIG_FILE..."
ctest --build-config "$BUILD_TYPE" $VERBOSE
echo "[PASS] Standard config tests successful."

# Run 2: config.ci.json
echo ""
echo "[2/2] Running tests with $CONFIG_CI_FILE..."
CI_CONFIG="$OAUTH2_SERVER_ABS_DIR/$CONFIG_CI_FILE"
TEST_WORK_DIR="$BUILD_ABS_DIR/$OAUTH2_SERVER_DIR/test"

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
