# Linux Build Guide

## Quick Start

### Option 1: Build with Drogon from Source (Recommended)

```bash
cd OAuth2Backend/scripts
./build.sh --build-drogon
```

This will:
1. Clone and build Drogon v1.9.13
2. Install Drogon to `/usr/local` (requires sudo)
3. Build OAuth2Backend with system libraries
4. Build all tests

### Option 2: Build with System Libraries

If you have Drogon already installed:

```bash
# Install system dependencies
sudo apt-get install -y \
    git gcc g++ cmake \
    libjsoncpp-dev uuid-dev libpq-dev \
    libssl-dev zlib1g-dev libhiredis-dev

# Build OAuth2Backend
cd OAuth2Backend/scripts
./build.sh
```

### Option 3: Build with Conan (Not Recommended)

```bash
# Install Conan
pip install conan

# Build with Conan
cd OAuth2Backend/scripts
./build.sh --conan
```

## Build Options

### Basic Usage

```bash
./build.sh                    # Default: Release build with system libraries
./build.sh Debug              # Debug build
./build.sh RelWithDebInfo     # Release with debug info
```

### Advanced Options

```bash
# Build Drogon from source
./build.sh --build-drogon

# Specify custom Drogon version
./build.sh --build-drogon --drogon-version=v1.9.10

# Specify custom install prefix
./build.sh --build-drogon --drogon-prefix=/opt/drogon

# Use Conan instead of system libraries
./build.sh --conan

# Combine options
./build.sh --build-drogon Debug --drogon-version=v1.9.13
```

## Running the Server

```bash
cd build/apps/server
./authforge-server
```

The server will start on `http://localhost:5555`.

## Running Tests

```bash
cd build
ctest --output-on-failure
```

## CI/CD Reference

This script follows the same build process as [`.github/workflows/ci-linux.yml`](../../.github/workflows/ci-linux.yml):

- Uses system libraries via apt-get
- Builds Drogon from source
- No Conan dependency on Linux
- Compatible with Ubuntu 20.04+ and Debian 11+

## Troubleshooting

### Missing Dependencies

If you see errors about missing libraries:

```bash
sudo apt-get install -y \
    git gcc g++ cmake \
    libjsoncpp-dev uuid-dev libpq-dev \
    libssl-dev zlib1g-dev libhiredis-dev
```

### Permission Denied

If you get "Permission denied" error:

```bash
chmod +x OAuth2Backend/scripts/build.sh
```

### Drogon Not Found

If CMake can't find Drogon:

```bash
# Option 1: Use --build-drogon flag
./build.sh --build-drogon

# Option 2: Install Drogon manually
sudo make install  # From Drogon build directory
```

### Sudo Password Required

When using `--build-drogon`, sudo is required to install Drogon to `/usr/local`. To avoid this:

```bash
# Install to user directory
./build.sh --build-drogon --drogon-prefix=$HOME/.local
```

## Comparison with Windows

- **Windows**: Uses `build.bat` with Conan (required)
- **Linux**: Uses `build.sh` with system libraries (recommended)
- **macOS**: Uses `build.sh` with Homebrew libraries

See [README.md](../../README.md) for more platform-specific information.
