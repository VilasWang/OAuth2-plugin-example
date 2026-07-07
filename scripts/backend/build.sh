#!/bin/bash
# build.sh - Build the backend project (Linux/macOS)
# Uses system package managers (apt/brew) instead of Conan, consistent with CI.

set -e

# Load common environment
source "$(dirname "$0")/env_common.sh"

BUILD_TYPE=Release
BUILD_DIR="$BUILD_ABS_DIR"
INSTALL_DEPS=false
BUILD_DROGON=false
DROGON_VERSION="v1.9.13"
SANITIZER=off

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

Show-Help() {
    echo "Usage: ./build.sh [options]"
    echo ""
    echo "Options:"
    echo "  --debug             Build in Debug mode"
    echo "  --release           Build in Release mode (default)"
    echo "  --install-deps      Install system dependencies (requires sudo/brew)"
    echo "  --build-drogon      Clone and build Drogon from source (as in CI)"
    echo "  --sanitizer=<kind>  Enable a sanitizer for the test target:"
    echo "                        off (default) | thread (TSan) | address (ASan)"
    echo "                        Implies --debug. TSan and ASan are mutually"
    echo "                        exclusive; run two builds to cover both."
    echo "  --tsan              Shortcut for --sanitizer=thread (implies --debug)"
    echo "  --asan              Shortcut for --sanitizer=address (implies --debug)"
    echo "  --help              Show this help"
}

for arg in "$@"; do
    case $arg in
        Debug|Release|RelWithDebInfo|MinSizeRel)
            BUILD_TYPE=$arg
            ;;
        --debug|-debug)
            BUILD_TYPE=Debug
            ;;
        --sanitizer=*)
            SANITIZER="${arg#*=}"
            # Sanitizers require a Debug build with frame pointers/symbols.
            BUILD_TYPE=Debug
            ;;
        --tsan)
            SANITIZER=thread
            BUILD_TYPE=Debug
            ;;
        --asan)
            SANITIZER=address
            BUILD_TYPE=Debug
            ;;
        --install-deps)
            INSTALL_DEPS=true
            ;;
        --build-drogon)
            BUILD_DROGON=true
            ;;
        --help|-h)
            Show-Help
            exit 0
            ;;
    esac
done

case "$SANITIZER" in
    off|thread|address) ;;
    *)
        echo -e "${RED}[Error] --sanitizer must be one of: off | thread | address (got '$SANITIZER')${NC}"
        exit 1
        ;;
esac

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Building Project (Linux/macOS) - Config: $BUILD_TYPE${NC}"
echo -e "${GREEN}========================================${NC}"

# 1. Install System Dependencies (Optional)
if [ "$INSTALL_DEPS" = true ]; then
    echo -e "${YELLOW}[INFO] Installing system dependencies...${NC}"
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        sudo apt-get update
        sudo apt-get install -y \
            git gcc g++ cmake libjsoncpp-dev uuid-dev libpq-dev \
            libssl-dev zlib1g-dev libhiredis-dev redis-tools \
            libcurl4-openssl-dev
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        brew install git cmake jsoncpp ossp-uuid zlib openssl@3 libpq hiredis curl
    else
        echo -e "${RED}[Error] Unsupported OS type: $OSTYPE${NC}"
        exit 1
    fi
fi

# 2. Build and Install Drogon (Optional, as in CI)
if [ "$BUILD_DROGON" = true ]; then
    echo -e "${YELLOW}[INFO] Building Drogon from source (${DROGON_VERSION})...${NC}"
    DROGON_TMP_DIR="/tmp/drogon_build"
    rm -rf "$DROGON_TMP_DIR"
    git clone --depth 1 --branch ${DROGON_VERSION} https://github.com/drogonframework/drogon "$DROGON_TMP_DIR"
    cd "$DROGON_TMP_DIR"
    git submodule update --init --recursive
    mkdir build && cd build
    
    CMAKE_DROGON_FLAGS="-DCMAKE_BUILD_TYPE=$BUILD_TYPE -DBUILD_EXAMPLES=OFF -DBUILD_MYSQL=OFF"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        export PostgreSQL_ROOT="$(brew --prefix libpq)"
        CMAKE_DROGON_FLAGS="$CMAKE_DROGON_FLAGS -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3) -DBUILD_POSTGRESQL=ON -DBUILD_REDIS=ON"
    fi
    
    cmake .. $CMAKE_DROGON_FLAGS
    make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
    sudo make install
    cd "$PROJECT_DIR"
    rm -rf "$DROGON_TMP_DIR"
fi

# 3. Configure and Build Project
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo -e "${YELLOW}[INFO] Configuring Project with CMake...${NC}"
CMAKE_PROJECT_FLAGS="-DCMAKE_BUILD_TYPE=$BUILD_TYPE -DBUILD_TESTS=ON -DCMAKE_CXX_STANDARD=17"

if [ "$SANITIZER" != "off" ]; then
    echo -e "${YELLOW}[INFO] Sanitizer enabled for test target: $SANITIZER${NC}"
    CMAKE_PROJECT_FLAGS="$CMAKE_PROJECT_FLAGS -DOAUTH2_SANITIZER=$SANITIZER"
fi

if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS specific paths
    export PostgreSQL_ROOT="$(brew --prefix libpq)"
    CMAKE_PROJECT_FLAGS="$CMAKE_PROJECT_FLAGS -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3) -DCURL_ROOT=$(brew --prefix curl) -DCMAKE_FIND_FRAMEWORK=LAST"
fi

# Configure: run cmake without aborting on error so we can show a friendly hint.
set +e
cmake "$PROJECT_DIR" $CMAKE_PROJECT_FLAGS
CMAKE_CONFIG_RC=$?
set -e
if [ $CMAKE_CONFIG_RC -ne 0 ]; then
    echo -e "${RED}[Error] CMake configuration failed.${NC}"
    echo -e "${YELLOW}Hint: a common cause is a missing Drogon or Jsoncpp system package.${NC}"
    echo -e "${YELLOW}      build.sh links against system packages (no Conan) on Linux/macOS.${NC}"
    echo -e "${YELLOW}      Run the one-time prerequisites first:${NC}"
    echo -e "${YELLOW}        ./manage.sh build-backend --install-deps${NC}"
    echo -e "${YELLOW}        ./manage.sh build-backend --build-drogon${NC}"
    exit 1
fi

echo -e "${YELLOW}[INFO] Building...${NC}"
cmake --build . --config $BUILD_TYPE -- -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# 4. Finalize
echo -e "${YELLOW}[INFO] Copying config files...${NC}"
mkdir -p "$BUILD_DIR/$OAUTH2_SERVER_DIR"
cp "$PROJECT_DIR/$OAUTH2_SERVER_DIR/$CONFIG_FILE" "$BUILD_DIR/$OAUTH2_SERVER_DIR/"
mkdir -p "$BUILD_DIR/$OAUTH2_SERVER_DIR/test"
cp "$PROJECT_DIR/$OAUTH2_SERVER_DIR/$CONFIG_FILE" "$BUILD_DIR/$OAUTH2_SERVER_DIR/test/"

echo -e "${GREEN}Build Completed Successfully!${NC}"
