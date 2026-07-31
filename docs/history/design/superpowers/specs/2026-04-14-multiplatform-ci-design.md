# Multi-Platform CI/CD Design for OAuth2 Plugin Example

**Date:** 2026-04-14  
**Author:** vilas  
**Status:** Design Approved - Pending Implementation

## 1. Overview

This document outlines the design for implementing a comprehensive multi-platform CI/CD pipeline for the OAuth2 Plugin Example project. The goal is to ensure code quality and functionality across Windows, Linux, and macOS platforms.

### 1.1 Objectives

- **Primary:** Full functional testing on all three major platforms (Windows, Linux, macOS)
- **Secondary:** Build artifact generation for distribution
- **Tertiary:** Performance optimization through intelligent caching

### 1.2 Scope

- **In Scope:** Backend C++ OAuth2 server testing and validation
- **Out of Scope:** Frontend Vue.js testing (existing Docker workflow covers this)
- **Testing Coverage:** Unit tests, integration tests, database persistence, RBAC, end-to-end OAuth2 flows

## 2. Architecture Design

### 2.1 Workflow Structure

```
.github/workflows/
├── ci.yml                    # Legacy Linux-only CI (keep for backup)
└── ci-multiplatform.yml      # New multi-platform CI (primary)
```

### 2.2 Matrix Strategy

```yaml
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
      
      # macOS - macos-14 (x86_64)
      - os: macos-14
        platform: macos
        build_type: Release
        cmake_generator: 'Unix Makefiles'
        package_manager: brew
        artifact_name: macos-clang-release
```

### 2.3 Execution Flow

1. **Parallel Execution:** All 3 platforms start simultaneously
2. **Service Containers:** Each platform spins up Docker PostgreSQL + Redis
3. **Dependency Installation:** Platform-specific package manager operations
4. **Build:** Unified CMake workflow with platform-specific parameters
5. **Testing:** Full CTest integration test suite
6. **Artifact Collection:** Upload test logs and build artifacts

## 3. Platform-Specific Configurations

### 3.1 Linux (ubuntu-22.04)

**Package Manager:** System apt repositories  
**Dependencies:**
- uuid-dev, libpq-dev, libjsoncpp-dev, libssl-dev, zlib1g-dev
- build-essential, python3-pip

**CMake Configuration:**
```bash
cmake -S OAuth2Backend -B build \
  -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=ON
```

**Key Points:**
- Direct system library usage, no Conan
- Reuses existing Ubuntu CI configuration
- PostgreSQL/Redis clients via apt

### 3.2 Windows (windows-2022)

**Package Manager:** Conan  
**Compiler:** MSVC 2022 (Visual Studio 17)

**CMake Configuration:**
```powershell
cmake -S OAuth2Backend -B build `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake `
  -DBUILD_TESTS=ON
```

**Key Points:**
- Conan handles all dependencies
- Conan toolchain provides library paths
- OpenSSL managed by Conan

### 3.3 macOS (macos-14)

**Package Manager:** Homebrew  
**Architecture:** x86_64 (forced for compatibility)

**Dependencies:**
```bash
brew install openssl@1.1 postgresql-client redis
```

**CMake Configuration:**
```bash
export OPENSSL_ROOT_DIR=$(brew --prefix openssl@1.1)
cmake -S OAuth2Backend -B build \
  -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="x86_64" \
  -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR}" \
  -DBUILD_TESTS=ON
```

**Key Points:**
- **Critical:** Force x86_64 architecture to avoid ARM linking issues
- Homebrew OpenSSL@1.1 (not system version)
- PostgreSQL client libraries via brew

### 3.4 Lessons Learned from Reference Project

From `qt-network-request-master` commit history:

1. **POSIX Compatibility:** `find -executable` is GNU-specific
   - **Issue:** macOS doesn't support GNU find flags
   - **Solution:** Use `-exec test -x {} \;` for cross-platform compatibility

2. **Architecture Consistency:** macOS ARM vs x86_64
   - **Issue:** linker errors on mixed architectures
   - **Solution:** Explicitly set `CMAKE_OSX_ARCHITECTURES="x86_64"`

3. **OpenSSL Detection:** Platform-specific paths
   - **Issue:** System OpenSSL vs Homebrew OpenSSL
   - **Solution:** Explicit `OPENSSL_ROOT_DIR` for all platforms

## 4. Docker Services (Unified)

### 4.1 PostgreSQL Service

```yaml
postgres:
  image: postgres:15-alpine
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
```

### 4.2 Redis Service

```yaml
redis:
  image: redis:alpine
  ports:
    - 6379:6379
  options: >-
    --health-cmd "redis-cli ping"
    --health-interval 10s
    --health-timeout 5s
    --health-retries 5
```

### 4.3 Cross-Platform Compatibility

- **Windows:** Use `postgres:15-alpine` instead of version-specific tags
- **All Platforms:** Unified health checks ensure service readiness
- **Environment Variables:** Consistent across all platforms

## 5. Testing Strategy

### 5.1 Database Initialization

**Service Readiness Check:**
```bash
# Linux/macOS
for i in $(seq 1 15); do
  pg_isready -h localhost -U oauth2_user && break
  echo "Postgres not ready yet (attempt $i/15)..."
  sleep 2
done

# Windows (PowerShell)
for ($i = 1; $i -le 15; $i++) {
  psql -h localhost -U oauth2_user -c "SELECT 1" -o $null 2>&1
  if ($LASTEXITCODE -eq 0) { break }
  Start-Sleep -Seconds 2
}
```

**Schema Initialization:**
```bash
export PGPASSWORD=123456
psql -h localhost -U oauth2_user -d oauth2_db -f OAuth2Backend/sql/001_oauth2_core.sql
psql -h localhost -U oauth2_user -d oauth2_db -f OAuth2Backend/sql/002_users_table.sql
psql -h localhost -U oauth2_user -d oauth2_db -f OAuth2Backend/sql/003_rbac_schema.sql
```

### 5.2 Test Execution

**Unified Command:**
```bash
cd OAuth2Backend/build
ctest -V -C Release --output-on-failure --timeout 120
```

**Test Coverage:**
1. **Unit Tests:** OAuth2 core logic (authorization code generation, token validation)
2. **Integration Tests:** PostgreSQL persistence
3. **Integration Tests:** Redis caching
4. **Integration Tests:** RBAC permission system
5. **End-to-End Tests:** Complete OAuth2 authorization flow

### 5.3 POSIX-Compliant File Collection

**Cross-Platform Artifact Collection:**
```bash
# Linux
find build/Release -type f \( -executable -o -name "*.so*" \) -exec cp {} artifacts/ \;

# Windows
find build/Release -type f \( -name "*.exe" -o -name "*.dll" \) -exec cp {} artifacts/ \;

# macOS (POSIX-compliant)
find build/Release -type f \( -name "*.dylib*" -o -exec test -x {} \; \) -exec cp {} artifacts/ \;
```

## 6. Caching Strategy

### 6.1 Drogon Build Cache

**Rationale:** Drogon build takes 10-15 minutes (clone + submodules + compilation)

**Cache Key Design:**
```yaml
key: drogon-${{ runner.os }}-${{ env.DROGON_VERSION }}-${{ matrix.platform }}-${{ env.BUILD_TYPE }}-${{ hashFiles('OAuth2Backend/conanfile.txt') }}
restore-keys: |
  drogon-${{ runner.os }}-${{ env.DROGON_VERSION }}-${{ matrix.platform }}-${{ env.BUILD_TYPE }}-
  drogon-${{ runner.os }}-${{ env.DROGON_VERSION }}-${{ matrix.platform }}-
```

**Benefits:**
- First build: 10-15 minutes
- Cache hit: 2-3 minutes
- Time savings: 80-85%

**Platform-Specific Builds:**
- **Linux/macOS:** `make -j$(nproc)` with system dependencies
- **Windows:** Visual Studio 2022 with Conan toolchain
- **macOS:** Explicit x86_64 architecture, Homebrew OpenSSL

### 6.2 Conan Package Cache (Windows Only)

```yaml
- name: Cache Conan packages
  if: matrix.platform == 'windows'
  uses: actions/cache@v4
  with:
    path: ~/.conan2
    key: conan-${{ runner.os }}-${{ hashFiles('OAuth2Backend/conanfile.txt') }}
```

### 6.3 CMake Build Cache

```yaml
- name: Cache CMake build
  uses: actions/cache@v4
  with:
    path: |
      OAuth2Backend/build
      OAuth2Backend/build/**/CMakeCache.txt
    key: ${{ matrix.artifact_name }}-cmake-${{ hashFiles('OAuth2Backend/CMakeLists.txt') }}
```

## 7. Error Handling and Monitoring

### 7.1 Failure Handling

**Strategy:**
- `fail-fast: false` - One platform failure doesn't affect others
- Automatic log upload on failure
- Platform-specific debugging steps

**Failure Categories:**
1. **Compilation Failures:** Code errors, missing dependencies
2. **Test Failures:** Logic errors, environment issues
3. **Timeout Failures:** Network issues, resource constraints
4. **Service Failures:** PostgreSQL/Redis connection problems

### 7.2 Platform-Specific Diagnostics

**Windows:**
```yaml
- name: Verify MSVC environment
  run: |
    cl 2>&1 | grep "Microsoft"
    conan --version
    conan list "OAuth2Backend/*"
```

**macOS:**
```yaml
- name: Check architecture compatibility
  run: |
    uname -m
    grep CMAKE_OSX_ARCHITECTURES OAuth2Backend/build/CMakeCache.txt
    brew --prefix openssl@1.1
```

**Linux:**
```yaml
- name: Check system libraries
  run: |
    ldconfig -p | grep libpq
    pkg-config --libs libpq openssl
```

### 7.3 Performance Monitoring

**Build Time Tracking:**
```yaml
- name: Start build timer
  run: echo "BUILD_START=$(date +%s)" >> $GITHUB_ENV

- name: Report build time
  if: always()
  run: |
    BUILD_END=$(date +%s)
    BUILD_TIME=$((BUILD_END - BUILD_START))
    echo "Build time: ${BUILD_TIME}s"
```

**Cache Statistics:**
```yaml
- name: Report cache statistics
  run: |
    echo "Conan Cache: ${{ steps.cache-conan.outputs.cache-hit }}"
    echo "Drogon Cache: ${{ steps.cache-drogon.outputs.cache-hit }}"
```

## 8. Artifact Management

### 8.1 Build Artifacts

**Upload on Completion:**
```yaml
- name: Upload build artifacts
  uses: actions/upload-artifact@v4
  with:
    name: ${{ matrix.artifact_name }}-binaries
    path: |
      OAuth2Backend/build/Release/*.exe
      OAuth2Backend/build/Release/*.dll
      OAuth2Backend/build/Release/oauth2-server
      OAuth2Backend/build/Release/*.so*
      OAuth2Backend/build/Release/*.dylib*
    retention-days: 7
```

### 8.2 Test Logs

**Upload on Failure:**
```yaml
- name: Upload test logs on failure
  if: failure()
  uses: actions/upload-artifact@v4
  with:
    name: ${{ matrix.artifact_name }}-test-logs
    path: |
      OAuth2Backend/build/Testing/
      OAuth2Backend/logs/
    retention-days: 7
```

## 9. Maintenance and Upgrades

### 9.1 Dependency Version Management

```yaml
env:
  DROGON_VERSION: "v1.9.10"
  POSTGRES_VERSION: "15"
  REDIS_VERSION: "alpine"
  CMAKE_VERSION: "3.25.0"
```

### 9.2 Update Strategy

- **Quarterly:** Major dependency updates (Drogon, PostgreSQL)
- **Monthly:** Security patches and bug fixes
- **Continuous:** Automated testing via PR validation

### 9.3 Rollback Mechanism

```yaml
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
```

## 10. Implementation Phases

### Phase 1: Foundation (Week 1)
- [ ] Create `ci-multiplatform.yml` workflow file
- [ ] Implement Linux matrix configuration
- [ ] Set up Docker services for all platforms
- [ ] Test PostgreSQL + Redis initialization

### Phase 2: Platform Expansion (Week 2)
- [ ] Add Windows matrix configuration with Conan
- [ ] Add macOS matrix configuration with Homebrew
- [ ] Implement platform-specific dependency installation
- [ ] Add POSIX-compliant file collection

### Phase 3: Optimization (Week 3)
- [ ] Implement Drogon build caching
- [ ] Add Conan package caching (Windows)
- [ ] Implement CMake build caching
- [ ] Add cache monitoring and statistics

### Phase 4: Monitoring and Polish (Week 4)
- [ ] Add platform-specific diagnostics
- [ ] Implement build time tracking
- [ ] Add PR comment integration
- [ ] Create rollback mechanisms

## 11. Success Criteria

### 11.1 Functional Requirements
- [PASS] All 3 platforms build successfully
- [PASS] Full test suite passes on all platforms
- [PASS] Database integration tests pass
- [PASS] Build artifacts generated correctly

### 11.2 Performance Requirements
- [PASS] CI runtime < 20 minutes per platform (cold cache)
- [PASS] CI runtime < 5 minutes per platform (warm cache)
- [PASS] Cache hit rate > 70%

### 11.3 Quality Requirements
- [PASS] Zero false negatives (passing builds marked as failed)
- [PASS] Actionable error messages for failures
- [PASS] Debug artifacts available within 5 minutes of failure

## 12. Risks and Mitigations

### 12.1 Technical Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| macOS ARM compatibility issues | High | Medium | Force x86_64 architecture |
| Conan package conflicts | Medium | Low | Pin dependency versions |
| Docker service startup failures | High | Low | Health checks with retries |
| Cache corruption | Medium | Low | Cache key includes dependency hashes |

### 12.2 Operational Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| GitHub Actions quota exhaustion | Medium | Low | Optimized caching strategy |
| Cache size limits (10GB) | Low | Low | Shallow clone and selective caching |
| Platform-specific bugs | High | Medium | Platform-specific diagnostics |

## 13. Alternatives Considered

### 13.1 Rejected Approaches

1. **Multi-file Workflows:** Rejected due to maintenance overhead
2. **Containerized Builds:** Rejected because it wouldn't test native platform compatibility
3. **Universal macOS Binary:** Rejected due to excessive build time

### 13.2 Rationale for Chosen Approach

- **Single-file matrix build:** Best balance of maintainability and clarity
- **Native platform testing:** Ensures real-world compatibility
- **Platform-specific package managers:** Leverages each platform's strengths

## 14. References

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Drogon Framework](https://github.com/drogonframework/drogon)
- [Reference Project: qt-network-request-master](file:///D:/work/development/Repos/qt-network-request-master)
  - Commit: `4ea2b4d` - POSIX-compatible find command
  - Commit: `9895677` - macOS ARM/x86_64 linking fix
  - Commit: `c0d0650` - Original multi-platform workflow

## 15. Appendix

### 15.1 Environment Variables

All platforms use consistent environment variables:
- `OAUTH2_DB_HOST`: Database host (default: "127.0.0.1")
- `OAUTH2_REDIS_HOST`: Redis host (default: "127.0.0.1")
- `OAUTH2_REDIS_PASSWORD`: Redis password (default: "")
- `PGPASSWORD`: PostgreSQL password for database initialization

### 15.2 Platform Matrix Summary

| Platform | Runner | Compiler | Package Manager | Architecture |
|----------|--------|----------|-----------------|--------------|
| Linux | ubuntu-22.04 | GCC | apt | x86_64 |
| Windows | windows-2022 | MSVC 2022 | Conan | x64 |
| macOS | macos-14 | Clang | Homebrew | x86_64 (forced) |

### 15.3 File Structure

```
authforge/
├── .github/
│   └── workflows/
│       ├── ci.yml                    # Legacy (keep backup)
│       └── ci-multiplatform.yml      # New implementation
├── OAuth2Backend/
│   ├── build/                        # CMake build output
│   ├── sql/                          # Database schemas
│   └── conanfile.txt                 # Conan dependencies
└── docs/
    └── superpowers/
        └── specs/
            └── 2026-04-14-multiplatform-ci-design.md
```

---

**Document Status:** Ready for Implementation  
**Next Steps:** Invoke writing-plans skill to create detailed implementation plan
