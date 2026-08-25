#!/usr/bin/env bash
# full-test.sh - One-click build + unit test + API test cycle (Linux/macOS)
#
# Prerequisites (one-time, must be done before the first run).
# build.sh drives everything through Conan + `cmake --preset`, so the C/C++
# libraries (Drogon, OpenSSL, jsoncpp, libpq, hiredis, ...) come from Conan --
# only the OS toolchain (compiler/cmake/git) and Conan itself must be present,
# and the database services must be up. Otherwise Step 3 (build) or Step 5
# (server start) will fail.
#
#   # 一次性前置(Linux 为例)
#   ./manage.sh build-backend --install-deps      # apt 装 git/build-essential/cmake
#   pipx install conan                            # 或 pip install conan
#   export FULLA_DB_USER=postgres                # 对齐你的 PG 账号(或创建 fulla_user)
#   export FULLA_DB_PASSWORD=<你的密码>
#   # 确保 PostgreSQL + Redis 服务在运行
#   # 之后才能:
#   ./manage.sh full-test -debug
set -euo pipefail

source "$(dirname "$0")/env_common.sh"

BUILD_TYPE="Release"
BUILD_ARG="--release"

for arg in "$@"; do
    case "$arg" in
        --debug|-debug) BUILD_TYPE="Debug"; BUILD_ARG="--debug" ;;
        --release|-release) BUILD_TYPE="Release"; BUILD_ARG="--release" ;;
    esac
done

FINAL_RESULT=0
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "Stopping server (PID $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

echo ""
echo "========================================"
echo "One-Click Build and Test ($BUILD_TYPE)"
echo "========================================"
echo ""

# Step 1: Reinitialize Database
echo "========================================"
echo "Step 1: Reinitializing fulla_db database"
echo "========================================"
bash "$SCRIPT_DIR/setup-database.sh"
echo "[SUCCESS] Database initialized"
echo ""

# Step 2: Regenerate ORM Models
echo "========================================"
echo "Step 2: Regenerating ORM models"
echo "========================================"
bash "$SCRIPT_DIR/generate-models.sh" -y
echo "[SUCCESS] ORM models regenerated"
echo ""

# Step 3: Rebuild Project
echo "========================================"
echo "Step 3: Rebuilding project"
echo "========================================"
bash "$SCRIPT_DIR/build.sh" "$BUILD_ARG"
echo "[SUCCESS] Project built"
echo ""

# Step 4: Run Tests
echo "========================================"
echo "Step 4: Running tests"
echo "========================================"
bash "$SCRIPT_DIR/test.sh" "$BUILD_ARG"
echo "[SUCCESS] All tests passed"
echo ""

# Step 5: Start Server
echo "========================================"
echo "Step 5: Starting OAuth2 server"
echo "========================================"
# Run from SERVER_BUILD_SUBDIR (build/apps/server): main.cc has no -c flag --
# it probes ./config.json relative to CWD, and build.sh stages config.json
# there. Multi-config generators (Ninja Multi-Config / Xcode) nest the binary
# under a per-config subdir, but config.json still lives in the parent, so we
# always cd to SERVER_RUN_DIR and invoke the binary by its full path.
SERVER_RUN_DIR="$BUILD_ABS_DIR/$(resolve_cmake_preset "$BUILD_TYPE")/$SERVER_BUILD_SUBDIR"
EXE_PATH="$SERVER_RUN_DIR/$SERVER_BINARY_NAME"
if [ ! -f "$EXE_PATH" ]; then
    EXE_PATH="$SERVER_RUN_DIR/$BUILD_TYPE/$SERVER_BINARY_NAME"
fi
if [ ! -f "$EXE_PATH" ]; then
    echo "[FAILED] Server executable not found"
    exit 1
fi

cd "$SERVER_RUN_DIR"
"$EXE_PATH" &
SERVER_PID=$!
echo "Server started (PID $SERVER_PID), waiting for startup..."
sleep 8

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[FAILED] Server failed to start or crashed"
    exit 1
fi
echo "[SUCCESS] Server started"
echo ""

# Step 6: Test OAuth2 Endpoints
echo "========================================"
echo "Step 6: Testing OAuth2 endpoints"
echo "========================================"
bash "$SCRIPT_DIR/test-oauth2-endpoints.sh" || FINAL_RESULT=1
if [ $FINAL_RESULT -eq 0 ]; then
    echo "[SUCCESS] OAuth2 endpoint tests passed"
fi
echo ""

# Step 7: Test Admin Endpoints
if [ $FINAL_RESULT -eq 0 ]; then
    echo "========================================"
    echo "Step 7: Testing Admin endpoints"
    echo "========================================"
    bash "$SCRIPT_DIR/test-admin-endpoints.sh" || FINAL_RESULT=1
    if [ $FINAL_RESULT -eq 0 ]; then
        echo "[SUCCESS] Admin endpoint tests passed"
    fi
    echo ""
fi

# Step 8: Stop Server
echo "========================================"
echo "Step 8: Stopping OAuth2 server"
echo "========================================"
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""
echo "[SUCCESS] Server stopped"
echo ""

if [ $FINAL_RESULT -ne 0 ]; then
    echo "========================================"
    echo "FULL TEST FAILED - see errors above"
    echo "========================================"
    exit 1
fi

echo "========================================"
echo "ALL STEPS COMPLETED SUCCESSFULLY!"
echo "========================================"
