# Backend Build Guide

All platforms (Windows, Linux, macOS) build the backend the same way:
**Conan resolves the C/C++ dependencies** (Drogon, OpenSSL, jsoncpp, libpq,
hiredis, ...) and **`cmake --preset` drives the configure/build**. Each preset
installs into its own `build/<preset-name>` directory (see
[`CMakePresets.json`](../../CMakePresets.json) `binaryDir`).

The `build.sh` / `build.bat` wrappers pick the right preset automatically from
the requested configuration, so you normally don't invoke Conan or CMake by
hand.

## Prerequisites (one-time)

- A C++17 toolchain: MSVC (Windows), GCC/Clang (Linux), Apple Clang (macOS)
- CMake ≥ 3.21 (required for the preset schema used here)
- [Conan](https://conan.io/) 2.x (`pipx install conan` or `pip install conan`)

On Linux/macOS the OS build toolchain can be bootstrapped with:

```bash
./build.sh --install-deps    # apt: git/build-essential/cmake ; brew: git/cmake
```

C/C++ libraries are **not** installed from apt/brew anymore — they come from
Conan.

## Quick Start

```bash
# Linux/macOS
./build.sh                    # Release build (linux-release / macos-arm64)
./build.sh --debug            # Debug build   (linux-debug / macos-arm64-debug)

# Sanitizer builds (imply --debug; GCC/Clang only)
./build.sh --asan             # AddressSanitizer (*-asan preset)
./build.sh --tsan             # ThreadSanitizer  (*-tsan preset)
```

```bat
REM Windows
build.bat                     REM Release build (windows-msvc)
build.bat -debug              REM Debug build   (windows-msvc-debug)
```

## Preset ⇄ build directory mapping

| Platform | Config | Preset | Build directory |
|----------|--------|--------|-----------------|
| Windows  | Release | `windows-msvc`        | `build/windows-msvc` |
| Windows  | Debug   | `windows-msvc-debug`  | `build/windows-msvc-debug` |
| Linux    | Release | `linux-release`       | `build/linux-release` |
| Linux    | Debug   | `linux-debug`         | `build/linux-debug` |
| macOS    | Release | `macos-arm64`         | `build/macos-arm64` |
| macOS    | Debug   | `macos-arm64-debug`   | `build/macos-arm64-debug` |

Sanitizers map to `linux-release-{asan,tsan}` / `macos-arm64-{asan,tsan}`.

## Manual build (equivalent to the wrappers)

```bash
# Example: Linux release
conan install . --output-folder=build/linux-release -s build_type=Release -s compiler.cppstd=17 --build=missing
cmake --preset linux-release
cmake --build --preset linux-release
```

`cmake --list-presets` shows every preset available on the current host.

## Running the Server

Use the helper (it resolves the preset directory for you):

```bash
./run-server.sh               # Release ; add --debug for the Debug build
```

Or run the binary directly from its preset directory, e.g. on Linux release:

```bash
cd build/linux-release/apps/server
./authforge-server
```

The server starts on `http://localhost:5555`. `main.cc` has no `-c` flag; it
loads `./config.json` relative to the working directory, which the build
wrappers stage next to the binary.

## Running Tests

```bash
./test.sh                     # Release ; add --debug for the Debug build
```

Or invoke `ctest` from the preset directory, e.g. on Linux release:

```bash
cd build/linux-release
ctest --output-on-failure
```

## CI/CD Reference

These scripts follow the same Conan + preset process as the CI workflows:

- [`.github/workflows/ci-linux.yml`](../../.github/workflows/ci-linux.yml) — `linux-release`
- [`.github/workflows/ci-macos.yml`](../../.github/workflows/ci-macos.yml) — `macos-arm64`
- [`.github/workflows/ci-windows.yml`](../../.github/workflows/ci-windows.yml) — `windows-msvc`

## Troubleshooting

### Conan not found

Install it and retry: `pipx install conan` (or `pip install conan`). The first
run also needs a profile: `conan profile detect`.

### `cmake --preset` fails to parse

`CMakeUserPresets.json` (Conan-generated, git-ignored) may hold a stale
`include` to a removed build directory. The build wrappers delete it before
`conan install`; if you build manually, remove it and re-run `conan install`.

### `drogon_ctl` not found at build time

`drogon_create_views()`/`drogon_create_model()` call `drogon_ctl` as a bare
command. `build.sh` adds the Conan package's `bin/` (read from
`build/<preset>/Drogon-*-data.cmake`) to `PATH` automatically; if you build by
hand, add that directory to `PATH` first.

### Permission denied on the scripts

```bash
chmod +x scripts/backend/build.sh
```

See the top-level [README.md](../../README.md) for the full project overview.
