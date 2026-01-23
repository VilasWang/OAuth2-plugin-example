#!/bin/bash
set -e

# Initialize variables
BUILD_TYPE=Release
USE_CONAN=false

# Parse arguments
for arg in "$@"; do
    case $arg in
        --conan)
            USE_CONAN=true
            shift
            ;;
        Debug|Release|RelWithDebInfo|MinSizeRel)
            BUILD_TYPE=$arg
            ;;
    esac
done

echo "Build Type: $BUILD_TYPE"
echo "Use Conan:  $USE_CONAN"

# Check dependencies
if ! command -v cmake &> /dev/null; then
    echo "Error: 'cmake' not found. Please install CMake."
    exit 1
fi

if [ "$USE_CONAN" = true ]; then
    if ! command -v conan &> /dev/null; then
        echo "Error: 'conan' not found. Please install Conan or install system libraries."
        exit 1
    fi
fi

# Determine source directory (where this script is)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
BUILD_DIR="$SCRIPT_DIR/build"

echo "Source Dir: $SCRIPT_DIR"
echo "Build Dir:  $BUILD_DIR"

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

CMAKE_ARGS="-DCMAKE_BUILD_TYPE=$BUILD_TYPE"

if [ "$USE_CONAN" = true ]; then
    # Install dependencies via Conan
    # Detecting profile if not exists (fail-safe)
    if ! conan profile path default &> /dev/null; then
        echo "Conan profile 'default' not found. Detecting..."
        conan profile detect
    fi

    echo "Installing Conan dependencies..."
    conan install "$SCRIPT_DIR" --build=missing -s build_type=$BUILD_TYPE

    # Configure CMake with Conan toolchain
    TOOLCHAIN_FILE="$BUILD_DIR/conan_toolchain.cmake"
    if [ -f "$TOOLCHAIN_FILE" ]; then
        echo "Using Conan toolchain: $TOOLCHAIN_FILE"
        CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE"
    fi
else
    echo "Using Native/System libraries (Skipping Conan)..."
fi

echo "Configuring CMake..."
cmake "$SCRIPT_DIR" $CMAKE_ARGS

# Build
echo "Building..."
cmake --build . --config $BUILD_TYPE -- -j$(nproc)

echo "Build Complete! Executable is in $BUILD_DIR"
