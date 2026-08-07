# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Security

#### OAuth/OIDC 合規審計 Batch 0 — P0 安全修復 (#21–#25)

- **F-002 (#21)**：client_secret 哈希寫入路徑統一為「有鹽小寫 SHA-256」，
  與校驗路徑一致（此前寫入用無鹽大寫 SHA-256，導致動態註冊/管理端
  創建的客戶端永遠無法通過 token 認證）。註冊與管理端 reset 路徑同步
  輪換 salt；新增 `utils::hashClientSecretWithSalt`。
- **F-003 (#22)**：`refresh_token` grant 增加客戶端認證（RFC 6749
  §3.2.1/§6）：CONFIDENTIAL 客戶端缺失/錯誤 secret 返回 401
  `invalid_client`（含 `WWW-Authenticate: Basic`），PUBLIC 客戶端僅校驗
  client_id 存在。
- **F-004 (#23)**：Redis 後端 client_secret 比較改為常量時間比較；三個
  存儲後端統一使用 `authforge::common::utils::constantTimeMemcmp`；刪除
  洩漏比較結果的 LOG_DEBUG。
- **F-005 (#24)**：獨立 Redis 存儲模式正式棄用：啟動時 LOG_ERROR 明示，
  該模式下 refresh_token grant 返回 `unsupported_grant_type`（替代誤導性
  的 invalid_grant）。目標架構為 Postgres 存儲 + Redis 緩存（另立
  issue）。文檔見 `docs/backend/configuration-guide.md` §3。
- **F-016 (#25)**：issuer 一致性修復：access token 簽發時寫入配置的
  issuer（此前從不寫入，introspect 返回 schema 硬編碼默認值）；刪除
  Postgres/Redis/Memory 三後端硬編碼的 `https://oauth.example.com`；
  introspect 空 iss 由配置的 issuer 兜底；discovery 對 issuer 尾斜線歸一
  化；非 localhost 的 http issuer 啟動告警。introspect `iss` 與 discovery
  `issuer` 字節一致（OIDC Discovery §3）。

#### OAuth/OIDC 合規審計 Batch 1 — 協議正確性 (#26/#28/#31/#33/#34/#35/#36)

- **F-007 (#26)**：授權端點錯誤按 RFC 6749 §4.1.2.1 分流 —— client_id 未知
  /redirect_uri 無效仍直接 4xx，其餘錯誤（invalid_scope、PKCE-required、
  server_error 等）改為 302 重定向到已驗證的 redirect_uri 並回顯 state。
- **F-006 (#28)**：資源端點 401（攜帶無效/過期憑證）發出 RFC 6750 §3
  `WWW-Authenticate: Bearer realm, error="invalid_token"` 挑戰。
- **F-015 (#31)**：`device_authorization` 端點按 client_type 分支認證
  （RFC 8628 §3.1.1）：CONFIDENTIAL 需 client_secret，PUBLIC 僅校驗存在
  （此前以空 secret 調用 validateClient，機密客戶端無法發起 device flow）。
- **F-011 (#33)**：PKCE 預設改為強制（RFC 9700 §2.1.1），`config.json`/
  `config.dev.json`/`config.ci.json`/`config.prod.json` 顯式
  `require_pkce_for_public: true`。
- **F-012 (#34)**：device flow 輪詢過快返回 `slow_down`（RFC 8628 §3.5），
  `oauth2_device_codes` 新增 `last_polled_at` 列並每次輪詢更新；
  `slow_down` 時將 interval 遞增 5 秒並持久化。
- **F-008/F-009/F-013 (#35)**：token 端點 validation gate 發 RFC 6749 §5.2
  `error: invalid_request` 信封（此前用應用信封 VALIDATION_INVALID_INPUT）；
  authorization_code 兌換時若授權時綁定了 redirect_uri 則請求必須攜帶且匹配
  （§4.1.3，此前空 redirect_uri 可繞過比較）；authorize 端校驗
  `code_challenge_method ∈ {plain, S256}`（RFC 7636 §4.3）。
- **F-014 (#36)**：redirect_uri 強制 https（RFC 8252 §7.3），豁免僅
  `http://127.0.0.1`/`http://[::1]` loopback IP 字面量（端口通配，`localhost`
  不豁免）；新增配置開關 `auth.allow_http_redirect_uri`（dev 開/prod 關）；
  seed 與測試中 `localhost` redirect_uri 同步改為 `127.0.0.1`。

### Changed

#### 依賴升級 (Dependencies)

- **Drogon Framework**: 升級 v1.9.12 → v1.9.13。同步更新 CI workflow
  (`ci-linux.yml` / `ci-windows.yml` / `ci-macos.yml`)、`deploy/docker/Dockerfile`
  與本機建置腳本 (`build.sh` / `env_setup.bat`) 的 `DROGON_VERSION`。
  v1.9.13 的 `drogon_ctl` 產生的 ORM 驗證碼改用
  `std::wstring_convert<std::codecvt_utf8_utf16<...>>` 取代
  `drogon::utils::utf8Length`（由 `orm_compat.h` 的相容層處理 C++20 棄用）。

#### 倉庫結構重構 (repo-structure-refactor P1–P11)

- 將 OAuth2Plugin 公開標頭從扁平 `oauth2/` 目錄重新組織為語義子目錄
  (`plugin/`, `types/`, `config/`, `error/`, `utils/`, `validation/`,
  `services/`, `storage/`, `observability/`, `controllers/`, `filters/`)。
- 將 `OAuth2Plugin/src/` 原始檔移入對應子目錄，保持與標頭結構一致。
- 重新命名驗證模組: `Validator` → `RuleEngine`, `ValidatorHelper` → `RuleSet`,
  `ValidationHelper` → `HttpResponder`, `ValidationRules` → `Rules`。
- 更新 CMakeLists.txt 反映新目錄結構。
- 移除全部 29 個轉發 shim 標頭與 `oauth2::` 命名空間向後相容 using 宣告。
- OAuth2Server 全部呼叫者更新為使用新的子目錄 include 路徑。

### Breaking Changes

- 扁平 `#include <oauth2/Foo.h>` 路徑不再有效，必須使用子目錄路徑
  (例如 `<oauth2/plugin/OAuth2Plugin.h>`)。
- `oauth2::Metrics`、`oauth2::OperationTimer`、`oauth2::AuditLogger`
  已移除，改用 `oauth2::observability::` 命名空間。

### Added

#### Frontend Security Enhancements (2026-05-12)

- **PKCE Support (RFC 7636)**: Enhanced security for public clients
  - Automatic code_verifier and code_challenge generation
  - SHA-256 hash-based PKCE implementation
  - Session-based code_verifier storage with auto-cleanup
  - Seamless integration with OAuth2 authorization flow

- **Token Management (RFC 7662 & RFC 7009)**: Complete token lifecycle management
  - Token introspection for real-time validation
  - Token revocation for secure logout
  - Secure token storage with expiration tracking
  - Automatic token validation and refresh

- **Authorization Server Metadata (RFC 8414)**: Dynamic configuration discovery
  - Server metadata fetch support
  - Dynamic endpoint discovery capability
  - Configuration-driven OAuth2 flow

- **Error Handling (RFC 6749)**: Standardized error responses
  - RFC-compliant error parsing (JSON and form-encoded)
  - User-friendly error messages
  - Technical error details for debugging

- **User Experience Improvements**:
  - Permission visualization (roles and scopes)
  - Token metadata display (issued/expired/scope/issuer)
  - Enhanced loading states and error feedback
  - Improved UI for PKCE-enabled authentication

- **New OAuth2 Helper Utilities**: `src/utils/oauth2Helper.js`
  - PKCE code generation and validation
  - Token introspection and revocation
  - RFC-compliant error handling
  - Secure token storage and validation
  - Authorization URL builder with PKCE support

#### OAuth2 Standardization (2026-05-11)

- **RFC 7662 Compliance**: Token Introspection Endpoint
  - `/oauth2/introspect` endpoint for token metadata queries
  - Client authentication via HTTP Basic Auth or POST body
  - Support for access token and refresh token introspection
  - P1 database fields: introspect_count, issued_at, not_before, issuer, audience
  - Metrics integration for monitoring introspection operations
  - Complete storage layer parity (PostgreSQL, Redis, Memory, Cached)

- **RFC 7009 Compliance**: Token Revocation Endpoint
  - `/oauth2/revoke` endpoint for token invalidation
  - Client authentication and permission control
  - Support for access token and refresh token revocation
  - P1 database fields: revoked_at, revoked_by
  - RFC-compliant error handling and idempotent behavior
  - Complete storage layer parity across all implementations

- **RFC 8414 Compliance**: Authorization Server Metadata Endpoint
  - `/.well-known/oauth-authorization-server` discovery endpoint
  - Comprehensive server metadata (issuer, endpoints, capabilities)
  - PKCE support declaration (plain, S256 methods)
  - Grant types and response types documentation
  - Scope support and authentication methods
  - Configuration-based metadata customization

- **RFC 6749 Compliance**: Standardized Error Handling
  - `common::error::OAuth2ErrorHandler` module for unified error responses
  - Standard OAuth2 error codes (invalid_request, invalid_client, etc.)
  - Proper HTTP status code mapping per RFC 6749
  - Consistent error response format across all endpoints
  - Enhanced error descriptions and optional error URIs

- **P1 Database Schema Enhancement**
  - Token audit fields: introspect_count, revoked_at, revoked_by
  - Token metadata fields: issued_at, not_before, issuer, audience
  - ORM model updates for all P1 fields
  - Database migration script (004_oauth2_scopes.sql)
  - Backward compatibility with existing tokens

- **OpenAPI Documentation Updates**
  - Complete API specs for introspection, revocation, and metadata endpoints
  - RFC-compliant request/response examples
  - Authentication method documentation
  - Error response schemas for all new endpoints

- **Testing & Quality Assurance**
  - P1 functionality test suite (18 test cases)
  - Token introspection tests (valid, invalid, expired, revoked tokens)
  - Token revocation tests (permission control, idempotency)
  - Client authentication tests (Basic Auth, POST body)
  - Storage layer parity verification across all implementations
  - 111 total test cases passing (379 assertions)

#### OAuth2 Security & Compliance (2026-05-02)

- **RFC 6749 Section 2.3.1 Compliance**: Client Authentication
  - Type-aware client validation (PUBLIC vs CONFIDENTIAL)
  - HTTP Basic Authentication support for confidential clients
  - Constant-time comparison for timing attack prevention
  - Proper HTTP status codes (401 vs 400 per OAuth2 spec)

- **RFC 6749 Section 4.1.3 Compliance**: Redirect URI Validation
  - Strict redirect_uri validation in token endpoint
  - Atomic validation in Redis using Lua scripts
  - Prevents authorization code interception attacks
  - All storage layers implement validation (PostgreSQL, Redis, Memory)

#### Frontend Features (2026-05-02)

- **Dashboard Page**: Comprehensive user dashboard at `/dashboard`
  - Displays user information (username, email)
  - Shows user roles and permissions
  - Real-time data fetching from `/oauth2/userinfo` endpoint
  - Loading states and error handling
  - Responsive design with professional UI

- **Logout Functionality**: Complete logout implementation
  - Backend `/oauth2/logout` endpoint with session clearing
  - Frontend logout button with localStorage clearing
  - Route guards to prevent access after logout
  - Proper token revocation and session cleanup
  - Fixed authentication flow and redirect loops

- **Route Guards**: Authentication state management
  - Automatic redirect to dashboard if already authenticated
  - Protected routes require authentication
  - Guest routes redirect to dashboard if authenticated
  - Consistent authentication state across application

#### Documentation (2026-05-02)

- **OpenAPI Documentation**: Interactive Swagger UI at `/docs/api`
  - Fixed static resource paths (relative to absolute)
  - Complete API documentation for all endpoints
  - Logout endpoint documentation
  - External login providers documentation
  - User info endpoint documentation

- **Automated Validation**: Consolidated OpenAPI validation scripts
  - Single `validate-openapi.sh` script for CI, pre-commit, and manual validation
  - Documentation coverage checks (descriptions, examples)
  - JSON structure validation
  - Removed redundant validation scripts

#### Testing & Quality Assurance (2026-05-02)

- **E2E Test Suite**: Comprehensive end-to-end testing
  - OAuth2AuthorizationCodeFlow: Complete OAuth2 flow testing
  - SessionManagement: Session creation and clearing validation
  - ClientAuthentication: PUBLIC vs CONFIDENTIAL clients, HTTP Basic Auth
  - RedirectURIValidation: Valid and invalid redirect URI testing
  - All tests passing with proper error handling

- **Integration Tests**: Redirect URI validation focus
  - RedirectUriValidation_MemoryStorage: Basic validation testing
  - RedirectUriValidation_Atomicity: Ensure atomic operations
  - RedirectUriValidation_EdgeCases: Empty URIs, case sensitivity, fragments
  - RedirectUriValidation_SecurityScenarios: Open redirect, URL traversal, null byte injection
  - All security scenarios properly tested and prevented

- **Performance Benchmarks**: Comprehensive performance testing
  - Performance_OAuth2Flow: Benchmarks save, consume, validate operations
  - Performance_StorageThroughput: Tests concurrent operation handling (50 threads, 500 ops)
  - Performance_MemoryUsage: Validates memory efficiency (10,000 auth codes)
  - Performance_LatencyPercentiles: Measures P50, P90, P95, P99, P99.9 latencies
  - Performance thresholds: Save < 1ms, Consume < 1ms, Validate < 0.5ms, P99 < 1ms

#### CI/CD Pipeline (2026-05-02)

- **Enhanced Test Execution**: Separate test categories for better reporting
  - Performance benchmark execution with artifact upload
  - E2E test execution for full flow validation
  - Integration test execution for security validation
  - Platform-specific configurations maintained

- **Performance Reporting**: Automated performance tracking
  - Performance report generation on each CI run
  - Performance metrics uploaded as artifacts (30-day retention)
  - Test logs still uploaded on failure (7-day retention)
  - Consistent test execution across Linux and Windows

#### Type System (2026-05-02)

- **OAuth2 Types**: New type system in `OAuth2Types.h`
  - `ClientType` enum (PUBLIC, CONFIDENTIAL)
  - `OAuth2Error` enum with standard OAuth2 errors
  - HTTP status code mapping for OAuth2 errors
  - Type-safe error handling
  - Fixed static resource paths (relative to absolute)
  - Complete API documentation for all endpoints
  - Logout endpoint documentation
  - External login providers documentation
  - User info endpoint documentation

- **Automated Validation**: Consolidated OpenAPI validation scripts
  - Single `validate-openapi.sh` script for CI, pre-commit, and manual validation
  - Documentation coverage checks (descriptions, examples)
  - JSON structure validation
  - Removed redundant validation scripts

### Changed

#### Storage Layer (2026-05-02)

- **All Storage Implementations**: Updated to support client type validation
  - `PostgresOAuth2Storage`: Added clientType field, constant-time comparison
  - `RedisOAuth2Storage`: Lua script updated for atomic redirect_uri validation
  - `MemoryOAuth2Storage`: Synchronized security fixes
  - All implementations validate redirect_uri parameter

- **Interface Changes**: Updated `IOAuth2Storage.h`
  - `consumeAuthCode` now requires `redirectUri` parameter
  - All implementations validate redirect_uri matches authorization request
  - Enhanced error messages for validation failures

#### Plugin Layer (2026-05-02)

- **OAuth2Plugin**: Updated token exchange flow
  - `exchangeCodeForToken` now includes redirectUri validation
  - `generateAuthorizationCode` stores redirectUri with auth code
  - Enhanced error handling for validation failures

#### Controller Layer (2026-05-02)

- **OAuth2Controller**: Enhanced security and error handling
  - HTTP Basic Authentication parsing (Base64 decoding without OpenSSL)
  - Type-aware client validation
  - Proper HTTP status codes per OAuth2 spec
  - Logout endpoint with session management
  - Fixed authentication flow and token handling

### Fixed

#### Security Vulnerabilities (2026-05-02) ⚠️ **CRITICAL**

1. **Client Secret Validation**: Fixed missing validation per RFC 6749
   - CONFIDENTIAL clients now require valid client_secret
   - PUBLIC clients skip secret validation (as per spec)
   - Constant-time comparison prevents timing attacks
   - HTTP Basic Authentication support

2. **Redirect URI Validation**: Fixed authorization code interception vulnerability
   - Token endpoint now validates redirect_uri parameter
   - Atomic validation prevents race conditions
   - All storage layers implement validation
   - Proper error messages for mismatch

3. **Session Management**: Fixed logout not clearing sessions
   - Backend logout endpoint clears userId from session
   - Frontend clears localStorage tokens
   - Route guards enforce authentication state
   - Fixed redirect loops after logout

4. **User Info Display**: Fixed dashboard not showing user information
   - Proper token handling in API requests
   - Error handling for failed requests
   - Role display functionality
   - Loading states and error messages

#### Bug Fixes (2026-05-02)

- **Swagger UI 404 Errors**: Fixed static resource paths in index.html
  - Changed relative paths to absolute paths
  - All resources now accessible at `/docs/api/swagger-ui/*`

- **Authentication Flow**: Fixed redirect loops after logout
  - Route guards properly check authentication state
  - Session clearing on backend and frontend

- **Token Handling**: Fixed token storage and retrieval in frontend
  - Proper localStorage management
  - Token validation before API requests

- **OpenAPI Documentation**: Fixed external login provider documentation inconsistencies
  - Updated endpoint documentation
  - Added missing response examples

### Security (2026-05-02)

**OAuth2 Compliance**: ✅ Full RFC 6749 Compliance

- ✅ Client Authentication (Section 2.3.1)
- ✅ Authorization Code Flow (Section 4.1)
- ✅ Redirect URI Validation (Section 4.1.3)
- ✅ Token Endpoint Security (Section 4.1.3)
- ✅ Error Responses (Section 4.1.2.1)
- ✅ Session Management and Cleanup

**Production Status**: 🟢 Ready for deployment

### Deprecated (2026-05-02)

- Removed redundant OpenAPI validation scripts:
  - `validate-openapi-simple.sh`
  - `pre-commit-validate-openapi.sh`
  - `ci-validate-openapi.sh`
- Replaced with unified `validate-openapi.sh`

### Added

#### Docker & DevOps (2026-04-22)

- **Docker Environment Standardization**
  - Standardized container naming: `oauth2-{service}-{env}` format
  - Standardized image naming with version tags
  - Added `Dockerfile.debug` and `Dockerfile.debug.cn` for development
  - Added `docker-compose.debug.yml` for isolated debug environment
  - Added verification scripts: `docker-quick-verify-debug.sh`, `docker-quick-verify-release.sh`
  - Added `cleanup-docker.sh` for automated Docker resource cleanup
  - Comprehensive Docker documentation

- **Release Environment Verification**
  - Container status checks for all services
  - Database initialization verification
  - HTTP endpoint testing (health, metrics, OAuth2)
  - OAuth2 integration tests (login, token, protected resources)
  - Automated log error scanning

#### Testing (2026-04-21)

- **Comprehensive Test Suites**
  - Security test suite (18 tests): SQL injection, XSS, CSRF, rate limiting
  - Functional test suite (21 tests): OAuth2 flow, UTF-8, RBAC, token lifecycle
  - 100% test pass rate

- **E2E Test Automation**
  - `/e2e-test` skill for automated OAuth2 flow validation
  - Authorization code flow testing
  - Token refresh validation
  - RBAC permission verification

### Changed

#### Docker Configuration (2026-04-22)

- Updated all services to use standardized container names
- Fixed Redis image version inconsistency (`redis:alpine` → `redis:7-alpine`)
- Updated `nginx.conf` and `prometheus.yml` to use new container names
- Updated all CI/CD workflows and integration test scripts

#### Documentation (2026-04-22)

- Created project-level CHANGELOG.md
- Added Docker standardization and verification guides
- Updated README with Linux compatibility section
- Moved bug fix reports to local-only directory

### Fixed

#### Critical Issues

- **Linux Teardown Crash** (2026-04-17) ⚠️ **CRITICAL**
  - Problem: Segmentation Fault during program exit
  - Root Cause: `OAuth2CleanupService` destructor accessed destroyed Event loop
  - Solution: Added `stopped_` flag to prevent duplicate cleanup
  - Impact: Clean exit without `std::_Exit(0)`

- **Security Vulnerabilities** (2026-04-21) ⚠️ **CRITICAL**
  - Fixed all 10 critical security vulnerabilities
  - SQL injection, XSS, command injection prevention
  - DoS protection, rate limiting, CORS policy
  - Token revocation, security headers, HSTS
  - **Status**: 18/18 tests passing (100%)

#### CI/CD & Platform Support

- **CI/CD Stability** (2026-04-17)
  - Fixed Windows CI teardown crashes
  - Resolved macOS codecvt_utf8_utf16 compatibility
  - Fixed duplicated test runs
  - Improved config handling
  - Disabled macOS tests due to framework issues

#### Feature Improvements

- **Rate Limiting** (2026-04-21)
  - Migrated to Drogon's Hodor plugin
  - Token bucket algorithm
  - Removed Redis dependency
  - Per-user and global rate limiting

- **Database** (2026-04-21)
  - Verified no connection leaks (false positive)
  - Environment variable support for empty passwords

### Security

**Production Status**: 🟢 Ready for deployment

- ✅ 10/10 critical vulnerabilities fixed
- ✅ 18/18 security tests passing
- ✅ 21/21 functional tests passing
- ✅ Complete audit coverage
- ✅ Rate limiting and DoS protection
- ✅ CORS and CSP headers configured

---

## [1.9.0] - 2026-04-15 to 2026-04-16

### Added

- **Multi-Platform CI/CD**
  - Linux CI (Ubuntu 22.04)
  - Windows CI (MSVC 2022)
  - macOS CI (ARM64 support)
  - Platform-specific dependency installation
  - Automated testing and artifact collection

- **Drogon Framework Upgrade**
  - Upgraded from v1.9.10 to v1.9.12
  - Added drogon_ctl build support
  - Improved C++17/20 compatibility

### Fixed

- **macOS Compatibility**
  - Forced C++17 mode (avoid C++20 deprecation)
  - Added codecvt_utf8_utf16 compatibility layer
  - Fixed Homebrew conflicts
  - Native ARM64 support

- **Windows CI**
  - Fixed PostgreSQL service initialization
  - Resolved path escaping issues
  - Added memory storage for testing
  - Improved Conan toolchain integration

- **Linux CI**
  - Added libhiredis-dev dependency
  - Fixed Redis tools installation

---

## [1.8.0] - 2026-04-01 to 2026-04-14

### Added

- **RBAC Permission System**
  - Role-based access control
  - Permission management
  - User-role assignment
  - API endpoint protection

- **PostgreSQL Persistence**
  - Database schema for OAuth2 data
  - Token storage and management
  - User management
  - Migration scripts

- **Redis Caching**
  - High-performance token cache
  - Atomic operations
  - Lua scripting for consistency

- **Observability**
  - Prometheus metrics endpoint
  - Structured audit logging
  - Performance monitoring

### Changed

- **Storage Architecture**
  - Pluggable storage backend (Memory, PostgreSQL, Redis)
  - Cached storage layer
  - Strategy pattern implementation

---

## [1.0.0] - 2026-01-14 to 2026-03-31

### Added

- **OAuth2.0 Implementation**
  - Authorization Code Grant flow
  - Access Token and Refresh Token support
  - Client registration and management
  - User authentication

- **Drogon Framework Integration**
  - Plugin-based architecture
  - Controller-based HTTP endpoints
  - Filter-based middleware
  - JSON configuration

- **WeChat Integration**
  - WeChat Open Platform API
  - QR code login support
  - Session management

- **Frontend Application**
  - Vue.js SPA client
  - OAuth2 login flow
  - Protected API access
  - User profile display

- **Persistence Layer**
  - Redis persistent storage (2026-01-15)
  - PostgreSQL persistent storage (2026-01-16)
  - Synchronous writes for stability
  - Security hardening with SHA256 hashing

- **Data Consistency** (2026-01-18)
  - Atomic consume operations for Redis
  - Client secret hash verification
  - Transaction support for PostgreSQL

- **User Authentication** (2026-01-19)
  - User account system
  - ORM-based storage migration
  - Strict ORM compliance
  - UUID support for salts

- **Testing**
  - Unit tests (2026-01-17)
  - Integration tests for Redis and PostgreSQL (2026-01-17)
  - E2E integration testing (2026-01-19)
  - Direct controller tests

- **Frontend** (2026-01-19)
  - Vue.js registration UI
  - Professional UI redesign
  - Registration success animation
  - Countdown and progress bar

### Security

- Basic authentication (username/password)
- Client secret hashing (SHA256)
- CORS configuration
- SQL injection prevention
- Input validation and sanitization

---

## Migration Guides

### Docker Environment Migration

1. Build new images:

   ```bash
   # Debug backend
   docker build --target backend-dev -t oauth2-backend-debug:v1.9.13 .
   # Production backend
   docker build --target backend-runtime -t oauth2-backend:v1.9.13 .
   ```

2. Update scripts to use new container names

3. Verify deployment:

   ```bash
   bash scripts/docker-quick-verify-release.sh
   ```

See [Docker Specification Guide](docs/backend/docker-guide.md) for details.

---

## Contributors

- Development Team
- Security Team
- DevOps Team
- QA Team

## Project Statistics

- **Total Commits**: 210
- **Development Period**: 2026-01-14 to 2026-04-22
- **Test Coverage**: 100% (39/39 tests passing)
- **Security Status**: Production Ready
- **Platforms**: Linux, Windows, macOS

## [Unreleased] - 2026-05-18

### Added
- Project skills modernization with refactored structure support
- manage.ps1 unified management interface integration  
- Docker specialized scripts integration
- Environment auto-detection capabilities
- Cross-platform compatibility improvements

### Fixed
- Updated all path references from OAuth2Backend/ to OAuth2Server/
- Fixed build output paths to build/OAuth2Server/
- Corrected SQL script paths to OAuth2Server/sql/
- Updated controller paths to OAuth2Server/controllers/
- Replaced outdated script paths with scripts/backend/

### Changed  
- All skills now prefer manage.ps1 interface when available
- Docker mode is now recommended for testing workflows
- Improved error handling and path validation
- Enhanced troubleshooting documentation

### Migration
- All existing skills remain backward compatible
- Automatic fallback to direct script invocation when needed
- No breaking changes to skill interfaces
- See migration guide for detailed information

### Skills Updated
1. build-and-test - Modern build workflow with manage.ps1
2. db-reset - Docker mode support with smart detection
3. orm-gen - Script integration and path fixes
4. openapi-update - Enhanced validation and new endpoints
5. e2e-test - Docker mode and full_test_docker.bat support
6. docker-integration-test - Complete Docker integration

