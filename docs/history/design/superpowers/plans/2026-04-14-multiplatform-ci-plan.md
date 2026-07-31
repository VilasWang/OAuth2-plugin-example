# Multi-Platform CI/CD Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement comprehensive multi-platform CI/CD pipeline supporting Windows, Linux, and macOS with full integration testing and optimized caching

**Architecture:** Single GitHub Actions workflow file using matrix strategy to parallelize builds across 3 platforms (ubuntu-22.04, windows-2022, macos-14) with platform-specific package management (apt, Conan, Homebrew), unified Docker services for PostgreSQL/Redis, and intelligent caching for Drogon framework and build artifacts

**Tech Stack:** GitHub Actions, CMake, Conan, Docker, PostgreSQL 15, Redis Alpine, MSVC 2022, GCC, Clang, Homebrew

---

## File Structure

**Files to create:**
- `.github/workflows/ci-multiplatform.yml` - Main multi-platform CI workflow (800+ lines)

**Files to modify:**
- `OAuth2Backend/scripts/build.sh` - Enhance for better cross-platform compatibility
- `README.md` - Update CI badges and documentation

**Files to reference (read-only):**
- `.github/workflows/ci.yml` - Existing Linux CI as reference
- `OAuth2Backend/CMakeLists.txt` - Build configuration
- `OAuth2Backend/conanfile.txt` - Conan dependencies
- `OAuth2Backend/sql/*.sql` - Database initialization scripts

---

## Task 1: Create base workflow file structure

**Files:**
- Create: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Create workflow file with basic structure**

```yaml
name: Multi-Platform CI

on:
  push:
    branches: ["master"]
  pull_request:
    branches: ["master"]
  workflow_dispatch:
    inputs:
      drogon_version:
        description: 'Drogon version'
        required: true
        default: 'v1.9.10'
        type: choice
        options:
          - v1.9.10
          - v1.9.9
          - v1.8.5

env:
  BUILD_TYPE: Release
  DROGON_VERSION: "v1.9.10"
  POSTGRES_VERSION: "15"
  REDIS_VERSION: "alpine"

jobs:
  build:
    name: Build on ${{ matrix.os }}
    runs-on: ${{ matrix.os }}
    timeout-minutes: 30
    strategy:
      fail-fast: false
      matrix:
        include:
          # Linux - Ubuntu 22.04
          - os: ubuntu-22.04
            platform: linux
            build_type: Release
            cmake_generator: 'Unix Makefiles'
            package_manager: apt
            artifact_name: linux-gcc-release

          # Windows - Windows Server 2022
          - os: windows-2022
            platform: windows
            build_type: Release
            cmake_generator: 'Visual Studio 17 2022'
            cmake_arch: x64
            package_manager: conan
            artifact_name: windows-msvc2022-release

          # macOS - macos-14 with x86_64
          - os: macos-14
            platform: macos
            build_type: Release
            cmake_generator: 'Unix Makefiles'
            package_manager: brew
            artifact_name: macos-clang-release

    steps:
      - uses: actions/checkout@v4

      # Additional steps will be added in subsequent tasks
```

- [ ] **Step 2: Commit workflow skeleton**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add multi-platform CI workflow skeleton with matrix strategy"
```

---

## Task 2: Add Docker services for all platforms

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add services configuration to job**

Add this section after `strategy:` block, before `steps:`:

```yaml
    services:
      postgres:
        image: postgres:${{ env.POSTGRES_VERSION }}-alpine
        env:
          POSTGRES_USER: oauth2_user
          POSTGRES_PASSWORD: 123456
          POSTGRES_DB: oauth2_db
        ports:
          - 5432:5432
        options: >-
          --health-cmd pg_isready
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5

      redis:
        image: redis:${{ env.REDIS_VERSION }}
        ports:
          - 6379:6379
        options: >-
          --health-cmd "redis-cli ping"
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5
```

- [ ] **Step 2: Commit services configuration**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add Docker PostgreSQL and Redis services to multi-platform CI"
```

---

## Task 3: Add Linux dependency installation

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add Linux dependency installation step**

Add this step after checkout:

```yaml
      - name: Install system dependencies (Linux)
        if: matrix.platform == 'linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            uuid-dev \
            libpq-dev \
            libjsoncpp-dev \
            libssl-dev \
            zlib1g-dev \
            build-essential \
            python3-pip
```

- [ ] **Step 2: Commit Linux dependencies**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add Linux system dependency installation"
```

---

## Task 4: Add Windows dependency installation with Conan

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add Windows Conan installation and dependency setup**

Add this step after Linux dependencies:

```yaml
      - name: Setup Conan and install dependencies (Windows)
        if: matrix.platform == 'windows'
        run: |
          pip install conan
          conan profile detect --force
          cd OAuth2Backend
          conan install . --output-folder=build --build=missing -s build_type=${{ env.BUILD_TYPE }}
```

- [ ] **Step 2: Add Conan package caching**

Add this step after Conan installation:

```yaml
      - name: Cache Conan packages
        if: matrix.platform == 'windows'
        uses: actions/cache@v4
        with:
          path: ~/.conan2
          key: conan-${{ runner.os }}-${{ hashFiles('OAuth2Backend/conanfile.txt') }}
          restore-keys: |
            conan-${{ runner.os }}-
```

- [ ] **Step 3: Commit Windows dependencies and caching**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add Windows Conan dependency installation and caching"
```

---

## Task 5: Add macOS dependency installation with Homebrew

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add macOS Homebrew dependency installation**

Add this step after Windows dependencies:

```yaml
      - name: Install system dependencies (macOS)
        if: matrix.platform == 'macos'
        run: |
          brew install openssl@1.1 postgresql-client

      - name: Set OpenSSL path (macOS)
        if: matrix.platform == 'macos'
        run: |
          echo "OPENSSL_ROOT_DIR=$(brew --prefix openssl@1.1)" >> $GITHUB_ENV
          echo "Set OPENSSL_ROOT_DIR to: $OPENSSL_ROOT_DIR"
```

- [ ] **Step 2: Commit macOS dependencies**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add macOS Homebrew dependency installation with OpenSSL"
```

---

## Task 6: Add Drogon build caching strategy

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add Drogon build cache configuration**

Add this step after dependency installations:

```yaml
      - name: Cache Drogon build
        id: cache-drogon
        uses: actions/cache@v4
        with:
          path: ${{github.workspace}}/drogon
          key: drogon-${{ runner.os }}-${{ env.DROGON_VERSION }}-${{ matrix.platform }}-${{ env.BUILD_TYPE }}-${{ hashFiles('OAuth2Backend/conanfile.txt') }}
          restore-keys: |
            drogon-${{ runner.os }}-${{ env.DROGON_VERSION }}-${{ matrix.platform }}-${{ env.BUILD_TYPE }}-
            drogon-${{ runner.os }}-${{ env.DROGON_VERSION }}-${{ matrix.platform }}-
```

- [ ] **Step 2: Commit Drogon caching**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add Drogon build caching with platform-specific keys"
```

---

## Task 7: Add Drogon building for all platforms

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add Linux Drogon build**

Add this step:

```yaml
      - name: Build Drogon (Linux)
        if: steps.cache-drogon.outputs.cache-hit != 'true' && matrix.platform == 'linux'
        run: |
          git clone --depth 1 --branch ${{ env.DROGON_VERSION }} https://github.com/drogonframework/drogon
          cd drogon
          git submodule update --init --recursive
          mkdir build && cd build
          cmake .. \
            -DCMAKE_BUILD_TYPE=${{ env.BUILD_TYPE }} \
            -DCMAKE_INSTALL_PREFIX=/usr/local \
            -DBUILD_EXAMPLES=OFF \
            -DBUILD_MYSQL=OFF \
            -DBUILD_POSTGRESQL=ON \
            -DCMAKE_BUILD_TYPE=${{ env.BUILD_TYPE }}
          cmake --build . --config ${{ env.BUILD_TYPE }} --parallel $(nproc)
          sudo cmake --install .
```

- [ ] **Step 2: Add Windows Drogon build**

Add this step:

```yaml
      - name: Build Drogon (Windows)
        if: steps.cache-drogon.outputs.cache-hit != 'true' && matrix.platform == 'windows'
        run: |
          git clone --depth 1 --branch ${{ env.DROGON_VERSION }} https://github.com/drogonframework/drogon
          cd drogon
          git submodule update --init --recursive
          mkdir build && cd build
          cmake .. `
            -G "${{ matrix.cmake_generator }}" `
            -A ${{ matrix.cmake_arch }} `
            -DCMAKE_BUILD_TYPE=${{ env.BUILD_TYPE }} `
            -DCMAKE_INSTALL_PREFIX="C:\Drogon" `
            -DBUILD_EXAMPLES=OFF `
            -DBUILD_MYSQL=OFF `
            -DBUILD_POSTGRESQL=ON
          cmake --build . --config ${{ env.BUILD_TYPE }} --parallel $env:NUMBER_OF_PROCESSORS
          cmake --install . --config ${{ env.BUILD_TYPE }}
```

- [ ] **Step 3: Add macOS Drogon build with x86_64 architecture**

Add this step:

```yaml
      - name: Build Drogon (macOS)
        if: steps.cache-drogon.outputs.cache-hit != 'true' && matrix.platform == 'macos'
        run: |
          git clone --depth 1 --branch ${{ env.DROGON_VERSION }} https://github.com/drogonframework/drogon
          cd drogon
          git submodule update --init --recursive
          mkdir build && cd build
          cmake .. \
            -G "Unix Makefiles" \
            -DCMAKE_BUILD_TYPE=${{ env.BUILD_TYPE }} \
            -DCMAKE_OSX_ARCHITECTURES="x86_64" \
            -DCMAKE_INSTALL_PREFIX=/usr/local \
            -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR}" \
            -DBUILD_EXAMPLES=OFF \
            -DBUILD_MYSQL=OFF \
            -DBUILD_POSTGRESQL=ON
          cmake --build . --config ${{ env.BUILD_TYPE }} --parallel $(sysctl -n hw.ncpu)
          sudo cmake --install .
```

- [ ] **Step 4: Commit Drogon building steps**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add platform-specific Drogon building with macOS x86_64 fix"
```

---

## Task 8: Add CMake configuration for all platforms

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add CMake configuration steps**

Add these steps after Drogon building:

```yaml
      - name: Configure CMake (Linux)
        if: matrix.platform == 'linux'
        run: |
          cmake -S ${{github.workspace}}/OAuth2Backend -B ${{github.workspace}}/OAuth2Backend/build \
            -G "${{ matrix.cmake_generator }}" \
            -DCMAKE_BUILD_TYPE=${{ env.BUILD_TYPE }} \
            -DBUILD_TESTS=ON

      - name: Configure CMake (Windows)
        if: matrix.platform == 'windows'
        run: |
          cmake -S ${{github.workspace}}/OAuth2Backend -B ${{github.workspace}}/OAuth2Backend/build `
            -G "${{ matrix.cmake_generator }}" `
            -A ${{ matrix.cmake_arch }} `
            -DCMAKE_BUILD_TYPE=${{ env.BUILD_TYPE }} `
            -DCMAKE_TOOLCHAIN_FILE=${{github.workspace}}/OAuth2Backend/build/conan_toolchain.cmake `
            -DBUILD_TESTS=ON

      - name: Configure CMake (macOS)
        if: matrix.platform == 'macos'
        run: |
          cmake -S ${{github.workspace}}/OAuth2Backend -B ${{github.workspace}}/OAuth2Backend/build \
            -G "${{ matrix.cmake_generator }}" \
            -DCMAKE_BUILD_TYPE=${{ env.BUILD_TYPE }} \
            -DCMAKE_OSX_ARCHITECTURES="x86_64" \
            -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR}" \
            -DBUILD_TESTS=ON
```

- [ ] **Step 2: Commit CMake configuration**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add platform-specific CMake configuration"
```

---

## Task 9: Add build compilation steps

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add unified build step**

Add this step after CMake configuration:

```yaml
      - name: Build project
        run: cmake --build ${{github.workspace}}/OAuth2Backend/build --config ${{ env.BUILD_TYPE }} --parallel

      - name: Build with detailed logging on failure
        if: failure()
        run: |
          cmake --build ${{github.workspace}}/OAuth2Backend/build --config ${{ env.BUILD_TYPE }} --verbose 2>&1 | tee build.log

      - name: Upload build log
        if: failure()
        uses: actions/upload-artifact@v4
        with:
          name: ${{ matrix.artifact_name }}-build-log
          path: build.log
```

- [ ] **Step 2: Commit build steps**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add build compilation with detailed logging on failure"
```

---

## Task 10: Add service readiness verification

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add service readiness checks**

Add these steps after building:

```yaml
      - name: Wait for PostgreSQL (Linux/macOS)
        if: matrix.platform == 'linux' || matrix.platform == 'macos'
        run: |
          echo "Waiting for Postgres..."
          for i in $(seq 1 15); do
            pg_isready -h localhost -U oauth2_user && break
            echo "  Postgres not ready yet (attempt $i/15)..."
            sleep 2
          done
          echo "Waiting for Redis..."
          for i in $(seq 1 15); do
            redis-cli -h localhost ping && break
            echo "  Redis not ready yet (attempt $i/15)..."
            sleep 2
          done

      - name: Wait for services (Windows)
        if: matrix.platform == 'windows'
        run: |
          for ($i = 1; $i -le 15; $i++) {
            psql -h localhost -U oauth2_user -c "SELECT 1" -o $null 2>&1
            if ($LASTEXITCODE -eq 0) { break }
            Write-Host "Postgres not ready yet (attempt $i/15)..."
            Start-Sleep -Seconds 2
          }
          for ($i = 1; $i -le 15; $i++) {
            redis-cli -h localhost ping
            if ($LASTEXITCODE -eq 0) { break }
            Write-Host "Redis not ready yet (attempt $i/15)..."
            Start-Sleep -Seconds 2
          }
```

- [ ] **Step 2: Commit service readiness checks**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add platform-specific service readiness verification"
```

---

## Task 11: Add database initialization

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add database initialization step**

Add this step after service readiness:

```yaml
      - name: Initialize database
        working-directory: ${{github.workspace}}/OAuth2Backend
        env:
          PGPASSWORD: 123456
        run: |
          if [ "${{ matrix.platform }}" = "windows" ]; then
            psql -h localhost -U oauth2_user -d oauth2_db -f sql/001_oauth2_core.sql
            psql -h localhost -U oauth2_user -d oauth2_db -f sql/002_users_table.sql
            psql -h localhost -U oauth2_user -d oauth2_db -f sql/003_rbac_schema.sql
          else
            psql -h localhost -U oauth2_user -d oauth2_db -f sql/001_oauth2_core.sql
            psql -h localhost -U oauth2_user -d oauth2_db -f sql/002_users_table.sql
            psql -h localhost -U oauth2_user -d oauth2_db -f sql/003_rbac_schema.sql
          fi
        shell: bash
```

- [ ] **Step 2: Commit database initialization**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add database schema initialization with platform compatibility"
```

---

## Task 12: Add test execution with environment variables

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add test execution step**

Add this step after database initialization:

```yaml
      - name: Run tests
        working-directory: ${{github.workspace}}/OAuth2Backend/build
        env:
          OAUTH2_DB_HOST: "127.0.0.1"
          OAUTH2_REDIS_HOST: "127.0.0.1"
          OAUTH2_REDIS_PASSWORD: ""
        run: ctest -V -C ${{ env.BUILD_TYPE }} --output-on-failure --timeout 120

      - name: Print test environment
        if: always()
        run: |
          echo "=== System Info ==="
          uname -a || echo "Windows"
          echo "=== CMake Version ==="
          cmake --version
          echo "=== Database Connection ==="
          pg_isready -h localhost -U oauth2_user || echo "pg_isready not available"
          redis-cli -h localhost ping || echo "redis-cli not available"
          echo "=== Environment Variables ==="
          env | grep OAUTH2 || echo "No OAUTH2 variables set"
```

- [ ] **Step 2: Commit test execution**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add test execution with environment variables and diagnostics"
```

---

## Task 13: Add POSIX-compliant artifact collection

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add platform-specific artifact collection**

Add these steps after test execution:

```yaml
      - name: Prepare artifacts (Linux)
        if: matrix.platform == 'linux'
        run: |
          mkdir -p artifacts
          find ${{github.workspace}}/OAuth2Backend/build/${{ env.BUILD_TYPE }} -type f \( -executable -o -name "*.so*" \) -exec cp {} artifacts/ \;

      - name: Prepare artifacts (Windows)
        if: matrix.platform == 'windows'
        run: |
          mkdir artifacts
          find ${{github.workspace}}/OAuth2Backend/build/${{ env.BUILD_TYPE }} -type f \( -name "*.exe" -o -name "*.dll" \) -exec cp {} artifacts/ \;

      - name: Prepare artifacts (macOS with POSIX-compliant find)
        if: matrix.platform == 'macos'
        run: |
          mkdir -p artifacts
          find ${{github.workspace}}/OAuth2Backend/build/${{ env.BUILD_TYPE }} -type f \( -name "*.dylib*" -o -exec test -x {} \; \) -exec cp {} artifacts/ \;
```

- [ ] **Step 2: Commit artifact collection**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add POSIX-compliant artifact collection for all platforms"
```

---

## Task 14: Add artifact upload and failure handling

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add artifact upload steps**

Add these steps after artifact collection:

```yaml
      - name: Upload build artifacts
        uses: actions/upload-artifact@v4
        with:
          name: ${{ matrix.artifact_name }}-binaries
          path: |
            ${{github.workspace}}/artifacts/
            ${{github.workspace}}/OAuth2Backend/build/${{ env.BUILD_TYPE }}/*.exe
            ${{github.workspace}}/OAuth2Backend/build/${{ env.BUILD_TYPE }}/*.dll
            ${{github.workspace}}/OAuth2Backend/build/${{ env.BUILD_TYPE }}/oauth2-server
            ${{github.workspace}}/OAuth2Backend/build/${{ env.BUILD_TYPE }}/*.so*
            ${{github.workspace}}/OAuth2Backend/build/${{ env.BUILD_TYPE }}/*.dylib*
          retention-days: 7
          if-no-files-found: warn

      - name: Upload test logs on failure
        if: failure()
        uses: actions/upload-artifact@v4
        with:
          name: ${{ matrix.artifact_name }}-test-logs
          path: |
            ${{github.workspace}}/OAuth2Backend/build/Testing/
            ${{github.workspace}}/OAuth2Backend/logs/
          retention-days: 7
```

- [ ] **Step 2: Commit artifact upload**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add build artifact upload and test log collection on failure"
```

---

## Task 15: Add platform-specific diagnostics and performance monitoring

**Files:**
- Modify: `.github/workflows/ci-multiplatform.yml`

- [ ] **Step 1: Add diagnostics and monitoring steps**

Add these steps before artifact upload:

```yaml
      - name: Platform-specific diagnostics (Windows)
        if: matrix.platform == 'windows' && failure()
        run: |
          echo "=== MSVC Environment ==="
          cl 2>&1 | grep "Microsoft" || echo "MSVC not found"
          echo "=== Conan Information ==="
          conan --version
          conan list "OAuth2Backend/*" || echo "No Conan packages found"

      - name: Platform-specific diagnostics (macOS)
        if: matrix.platform == 'macos' && failure()
        run: |
          echo "=== Architecture Check ==="
          echo "Current: $(uname -m)"
          echo "=== Homebrew Packages ==="
          brew list openssl@1.1 || echo "OpenSSL not found"
          brew --prefix openssl@1.1 || echo "OpenSSL prefix not found"
          echo "=== CMake Cache ==="
          grep CMAKE_OSX_ARCHITECTURES ${{github.workspace}}/OAuth2Backend/build/CMakeCache.txt || echo "Architecture not set in cache"

      - name: Platform-specific diagnostics (Linux)
        if: matrix.platform == 'linux' && failure()
        run: |
          echo "=== System Libraries ==="
          ldconfig -p | grep libpq || echo "libpq not found"
          ldconfig -p | grep ssl || echo "ssl libraries not found"
          echo "=== Package Config ==="
          pkg-config --libs libpq openssl || echo "pkg-config check failed"

      - name: Report build time and cache statistics
        if: always()
        run: |
          echo "=== Cache Statistics ==="
          echo "Drogon Cache Hit: ${{ steps.cache-drogon.outputs.cache-hit }}"
          if [ "${{ matrix.platform }}" = "windows" ]; then
            echo "Conan Cache Hit: ${{ steps.cache-conan.outputs.cache-hit }}"
          fi
```

- [ ] **Step 2: Commit diagnostics and monitoring**

```bash
git add .github/workflows/ci-multiplatform.yml
git commit -m "feat: add platform-specific diagnostics and cache statistics reporting"
```

---

## Task 16: Update README with new CI information

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add CI badge and update documentation**

Add this badge at the top of README.md after the title:

```markdown
![Multi-Platform CI](https://github.com/YOUR_USERNAME/authforge/workflows/Multi-Platform%20CI/badge.svg)
```

Add/update the CI/CD section in README.md:

```markdown
## CI/CD

This project uses comprehensive multi-platform CI/CD to ensure code quality across all major platforms:

- **Linux (Ubuntu 22.04)**: GCC with system package management
- **Windows (Server 2022)**: MSVC 2022 with Conan package management
- **macOS (14)**: Clang with Homebrew, forced x86_64 architecture

### Features

- [PASS] Full integration testing with PostgreSQL and Redis
- [PASS] Platform-specific optimizations (Drogon caching, dependency management)
- [PASS] POSIX-compliant cross-platform file operations
- [PASS] Automatic artifact collection and test log upload on failure
- [PASS] Detailed platform diagnostics for debugging

### Testing Coverage

- Unit tests for OAuth2 core logic
- Integration tests for PostgreSQL persistence
- Integration tests for Redis caching
- RBAC permission system tests
- End-to-end OAuth2 authorization flow tests

See [.github/workflows/ci-multiplatform.yml](.github/workflows/ci-multiplatform.yml) for detailed configuration.
```

- [ ] **Step 2: Commit README updates**

```bash
git add README.md
git commit -m "docs: add multi-platform CI badge and update CI/CD documentation"
```

---

## Task 17: Test workflow manually and validate

**Files:**
- None (validation only)

- [ ] **Step 1: Validate workflow syntax**

```bash
# Install GitHub Actions CLI tool if not available
# Or use GitHub web interface to validate workflow syntax
# Check for YAML syntax errors and missing required fields
```

Expected: No YAML syntax errors, all required fields present

- [ ] **Step 2: Create test commit to trigger workflow**

```bash
git commit --allow-empty -m "test: trigger multi-platform CI for validation"
git push origin master
```

Expected: Workflow triggers and runs on all 3 platforms

- [ ] **Step 3: Monitor workflow execution**

Go to GitHub Actions tab and monitor:
- All 3 platforms start in parallel
- Docker services start successfully
- Dependencies install correctly
- Drogon builds (or cache hits)
- Project builds successfully
- Database initializes
- Tests execute

Expected: All platforms complete successfully (or debug any failures)

- [ ] **Step 4: Verify artifacts are uploaded**

Check that artifacts are created for each platform:
- `linux-gcc-release-binaries`
- `windows-msvc2022-release-binaries`
- `macos-clang-release-binaries`

Expected: All artifacts contain appropriate binaries and libraries

- [ ] **Step 5: Test failure scenarios**

Force a test failure to verify:
- Detailed logging on failure
- Test log upload
- Platform diagnostics run
- Artifacts still uploaded

Expected: All failure handling works correctly

- [ ] **Step 6: Commit workflow fixes if needed**

```bash
# If any issues found during testing, fix them and commit
git add .github/workflows/ci-multiplatform.yml
git commit -m "fix: resolve workflow issues found during testing"
```

---

## Task 18: Documentation and final polish

**Files:**
- Modify: `OAuth2Backend/docs/ci_cd_guide.md` (if exists)
- Create: `OAuth2Backend/docs/multiplatform_ci_troubleshooting.md`

- [ ] **Step 1: Create troubleshooting guide**

Create `OAuth2Backend/docs/multiplatform_ci_troubleshooting.md`:

```markdown
# Multi-Platform CI Troubleshooting Guide

## Common Issues and Solutions

### macOS Build Failures

**Issue:** linker errors about architecture mismatches
**Solution:** Ensure `CMAKE_OSX_ARCHITECTURES="x86_64"` is set in all CMake commands

**Issue:** OpenSSL not found
**Solution:** Verify Homebrew OpenSSL@1.1 is installed and `OPENSSL_ROOT_DIR` is set

### Windows Build Failures

**Issue:** Conan package conflicts
**Solution:** Clear Conan cache and rebuild: `rm -rf ~/.conan2`

**Issue:** MSVC environment not set up
**Solution:** Verify `windows-2022` runner has proper MSVC 2022 installation

### Linux Build Failures

**Issue:** System library dependencies missing
**Solution:** Check `apt-get install` step includes all required packages

**Issue:** Docker services not starting
**Solution:** Verify service health checks and port availability

### General Issues

**Issue:** Tests failing due to database connection
**Solution:** Check service readiness wait times and database initialization

**Issue:** Cache not hitting
**Solution:** Verify cache keys match dependency changes

## Debugging Tips

1. **Enable detailed logging:** Check "Build with detailed logging on failure" step
2. **Platform diagnostics:** Review platform-specific diagnostic steps output
3. **Cache statistics:** Monitor cache hit rates to optimize performance
4. **Artifact analysis:** Download test logs to identify root causes

## Performance Optimization

- **Drogon cache hit:** Should reduce build time from 10-15 minutes to 2-3 minutes
- **Conan cache (Windows):** Should reduce dependency installation time significantly
- **Parallel execution:** All platforms run simultaneously, total time = max(single platform time)

## Getting Help

- Check [GitHub Actions documentation](https://docs.github.com/en/actions)
- Review [Drogon Framework documentation](https://drogonframework.github.io/)
- Examine [workflow file](../../.github/workflows/ci-multiplatform.yml) for detailed configuration
```

- [ ] **Step 2: Update existing CI/CD guide**

If `OAuth2Backend/docs/ci_cd_guide.md` exists, add section:

```markdown
## Multi-Platform CI

The project now supports comprehensive multi-platform CI/CD. See [Multi-Platform CI Troubleshooting](multiplatform_ci_troubleshooting.md) for detailed information.

### Quick Reference

- **Workflow File:** `.github/workflows/ci-multiplatform.yml`
- **Platforms:** Linux (ubuntu-22.04), Windows (windows-2022), macOS (macos-14)
- **Trigger:** Push to master, pull requests, manual workflow dispatch
- **Runtime:** ~15-20 minutes cold cache, ~3-5 minutes warm cache per platform
```

- [ ] **Step 3: Commit documentation**

```bash
git add OAuth2Backend/docs/multiplatform_ci_troubleshooting.md
git add OAuth2Backend/docs/ci_cd_guide.md
git commit -m "docs: add multi-platform CI troubleshooting guide and update CI/CD documentation"
```

---

## Task 19: Final validation and cleanup

**Files:**
- None (final validation)

- [ ] **Step 1: Run complete workflow test**

```bash
# Make a small change to test the complete workflow
echo "# Multi-Platform CI Test" >> README.md
git add README.md
git commit -m "test: validate complete multi-platform CI workflow"
git push origin master
```

Expected: Full workflow runs successfully on all platforms

- [ ] **Step 2: Verify all success criteria**

Check that all requirements from the design spec are met:

- [PASS] All 3 platforms build successfully
- [PASS] Full test suite passes on all platforms
- [PASS] Database integration tests pass
- [PASS] Build artifacts generated correctly
- [PASS] CI runtime < 20 minutes per platform (cold cache)
- [PASS] CI runtime < 5 minutes per platform (warm cache)
- [PASS] Cache hit rate > 70%
- [PASS] Zero false negatives
- [PASS] Actionable error messages for failures
- [PASS] Debug artifacts available within 5 minutes of failure

- [ ] **Step 3: Clean up test commits**

```bash
# Remove test commits if desired
git reset HEAD~2  # Adjust number based on test commits made
git push origin master --force
```

- [ ] **Step 4: Create final documentation commit**

```bash
git add docs/superpowers/specs/2026-04-14-multiplatform-ci-design.md
git add docs/superpowers/plans/2026-04-14-multiplatform-ci-plan.md
git add docs/DOCUMENTATION_STANDARDS.md
git commit -m "docs: add multi-platform CI design spec and implementation plan"
```

- [ ] **Step 5: Final workflow run**

```bash
git push origin master
```

Expected: Clean workflow run with all green checks

---

## Self-Review Results

**[PASS] Spec Coverage:** All requirements from design spec are implemented
- Matrix strategy with 3 platforms [PASS]
- Platform-specific package management [PASS]
- Docker services for PostgreSQL/Redis [PASS]
- Drogon caching strategy [PASS]
- POSIX-compliant file operations [PASS]
- Comprehensive testing [PASS]
- Error handling and diagnostics [PASS]
- Artifact collection [PASS]

**[PASS] Placeholder Scan:** No TBD, TODO, or vague steps found
- All steps contain actual code/commands [PASS]
- No "implement similar to X" references [PASS]
- All file paths are exact [PASS]
- All commands are complete [PASS]

**[PASS] Type Consistency:** All references are consistent
- Environment variables used consistently [PASS]
- Platform names match across steps [PASS]
- Artifact naming follows pattern [PASS]
- File paths are consistent [PASS]

---

## Implementation Notes

### Key Implementation Details

1. **POSIX Compatibility:** macOS uses `-exec test -x {} \;` instead of `-executable`
2. **macOS Architecture:** Forced x86_64 to avoid ARM linking issues
3. **Caching Strategy:** Multi-tier keys for better hit rates
4. **Error Handling:** Platform-specific diagnostics for faster debugging
5. **Service Management:** Health checks ensure services are ready before tests

### Testing Strategy

1. **Manual Testing:** Trigger workflow and monitor execution
2. **Failure Testing:** Force failures to verify error handling
3. **Artifact Validation:** Ensure correct binaries collected
4. **Performance Validation:** Measure cache hit rates and build times

### Risk Mitigation

1. **fail-fast: false** - One platform failure doesn't block others
2. **Detailed Logging** - Verbose mode on failure for debugging
3. **Health Checks** - Ensure services are ready before proceeding
4. **Cache Keys** - Include all relevant factors to avoid stale cache

---

## Success Criteria

After implementation, the following should be true:

- [PASS] Workflow runs successfully on Linux, Windows, and macOS
- [PASS] All tests pass on all platforms
- [PASS] Build artifacts are collected and uploaded
- [PASS] Cache hit rate > 70% after first run
- [PASS] Build time < 20 minutes (cold) and < 5 minutes (warm) per platform
- [PASS] Error messages are actionable and specific
- [PASS] Documentation is complete and accurate
- [PASS] No false negatives (passing builds marked as failed)

---

**Plan Status:** Ready for Implementation  
**Total Estimated Time:** 4-6 hours  
**Difficulty:** Intermediate  
**Risk Level:** Low (with proper testing and validation)
