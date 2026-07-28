# CODEBUDDY.md

authforge — Production-grade OAuth2.0/OIDC authorization server (Drogon / C++17,
PostgreSQL, Redis, Vue.js frontends). Supports RFC 6749, 7662, 7009, 8414, 8628, 7591.

## Build & Test (non-standard flags only)

Standard build / run / test goes through the unified wrappers
`./manage.sh` / `./manage.ps1` — see `README.md` "Quick Start", or the
`/build-and-test` skill. Only the non-obvious flags are listed here:

- Debug build: append `-debug`. Rebuild Drogon from source: `--build-drogon`.
  Sanitizers: `--sanitizer=thread|address` (Linux/macOS only).
- No-external-DB test build: `-DOAUTH2_MEMORY_TESTS_ONLY=ON`.
- Run C++ tests by label: `ctest -R Unit|Integration|E2E|Security|Performance`.
- Windows backend builds via Conan (`scripts/backend/build.bat`);
  Linux/macOS via `scripts/backend/build.sh`.

## Architecture

### Key Patterns

- **Plugin-based DI**: `OAuth2Plugin` is a Drogon `HttpPlugin<>` singleton.
  `initAndStart()` creates the storage, services (TokenService, ClientService,
  IdentityService), JwkManager and CleanupService. Access them via
  `drogon::app().getPlugin<OAuth2Plugin>()`.
- **Storage Strategy**: `IOAuth2Storage` is the abstract interface; `storage_type`
  in config selects the backend — `postgres` →
  `CachedOAuth2Storage(PostgresOAuth2Storage + Redis)` (production), `redis` →
  `RedisOAuth2Storage` (cache-only), `memory` → `MemoryOAuth2Storage` (testing,
  no external deps). `CachedOAuth2Storage` wraps any backend with a Redis L2 cache.
- **Async callbacks**: all storage/service methods are async with
  `std::function<void(result)> &&callback` as the last parameter. Services hold
  `shared_ptr<IOAuth2Storage>` for lifetime; async continuations capture
  `auto self = shared_from_this()` to prevent use-after-free.
- **Error system**: `Error` → `ErrorCatalog` (single source for error codes and
  messages) → `ErrorResponder` renders JSON error envelopes. Codes are stable
  strings (e.g. `AUTH_INVALID_CREDENTIALS`), never integers.

### Layer Separation

Controllers (HTTP) → Plugin/Service (business) → Storage (data) → Model (ORM).
Use Drogon built-ins over third-party libraries.

## Critical Rules

Path-scoped hard constraints and workflow entry points live in `.codebuddy/rules/`
and auto-load when you touch matching files:
- `orm-models.md` — never hand-edit `models/**`; regenerate with `/orm-gen`.
- `db-operations.md` — DB access is the async + Mapper + Criteria combo; raw SQL only for DDL / `UPDATE...RETURNING` / documented batch.
- `data-access.md` — pointer to `db-operations.md` for storage code.
- `dev-workflow.md` — dev commands: prefer `./manage.sh` / `./manage.ps1`; backend rebuild-DB / ORM / build / test / endpoint tests, frontend build / run / test.

`git push` is forbidden (human review required) and enforced as a deny rule in
`.codebuddy/settings.json`.

## Coding Conventions

### Async Programming
| Pattern | Status |
|---------|--------|
| Async callbacks (`Mapper::findOne`, `execSqlAsync`) | REQUIRED — always prefer |
| Synchronous (`Mapper::findBy` with future) | RESTRICTED — only when truly needed |
| Coroutines (`CoroMapper`) | FORBIDDEN — never use |

### Lambda Capture Rules
- `[sharedCb]` — REQUIRED for callback lifetime
- `[&var]` — FORBIDDEN unless PR explains lifetime guarantee
- `[this]` — FORBIDDEN (no PR-exemption); use `shared_from_this()` instead — the
  class must `enable_shared_from_this<T>` and the lambda captures
  `auto self = shared_from_this()`, holding ownership so `this` stays alive for
  the whole async continuation.

### Callback Pattern
```cpp
auto sharedCb = std::make_shared<std::function<void(const ResultType &)>>(
    std::move(callback));
// Use *sharedCb to invoke
```

### Data Access
| Operation | Allowed | Method |
|-----------|---------|--------|
| SELECT | ORM only | `Mapper::findBy`, `Mapper::findOne` |
| INSERT | ORM only | `Mapper::insert` |
| UPDATE | ORM only | `Mapper::update` |
| JOIN | Forbidden | Split into multiple queries or `Criteria::In` |
| Raw SQL | Exception only | DDL, `UPDATE...RETURNING`, batch ops |

### Code Style
- C++17 standard, Google style, 100 char line limit
- clang-format runs automatically on edit (hook configured)
- ASCII only in code: use `[+]`, `[-]`, `[!]` instead of emoji
- No comments explaining WHAT — name variables/functions to be self-documenting
- Comments only for WHY: hidden constraints, non-obvious invariants, workarounds

### Error Handling
- Always catch `const DrogonDbException &e` for DB operations
- All async callbacks MUST handle failure path: `(*sharedCb)(errorResult)`
- Log levels: `LOG_DEBUG` (dev), `LOG_INFO` (flow), `LOG_WARN` (issues), `LOG_ERROR` (failures)
- NEVER log passwords, tokens, or secrets

### Security
- Input validation on ALL user input
- ORM Criteria for queries (no string concatenation)
- SHA-256 + salt for password/client secret hashing
- Token TTL: access 1h, refresh 30d
- PKCE required for public clients
- Rate limiting on login/token/password-reset endpoints

## Configuration

- Sensitive values via env vars: `OAUTH2_DB_PASSWORD`, `OAUTH2_REDIS_PASSWORD`.
- No-external-DB test build flag: `-DOAUTH2_MEMORY_TESTS_ONLY=ON`.
- Config files: `apps/server/config/config*.json` + `config.{dev,ci,prod}.json` overrides.

## Test Architecture

Tests live in `apps/server/test/`; category labels and priorities are defined
authoritatively in `test/common/test_categories.h`.

- `TestBase.h` provides `TestTransaction` (RAII rollback wrapper) — prefer it
  over manual rollback so every test reverts its DB changes.
- CI runs on three platforms: Linux (GCC + PostgreSQL + Redis), Windows
  (MSVC 2022, memory-storage only), macOS (Clang ARM64, build verification only).
- Framework: Google Test via Drogon (`drogon_test.h`)
- Coverage target: 80%+
- Test naming: `{Unit|Integration|Security}_{Module}_{Function}_{Scenario}`

## Dev Workflow

Prefer `./manage.sh` / `./manage.ps1` for any backend task:

| Step | Run |
|------|-----|
| Rebuild database | `/db-reset` skill |
| Regenerate ORM models | `./manage.sh generate-models` — the `/orm-gen` skill wraps this |
| Build | `./manage.sh build-backend` (`-debug` for Debug) |
| Run server | `./manage.sh run-backend` (`-debug`) |
| Unit / integration tests | `./manage.sh test-backend` (`-debug`) |
| Endpoint API tests | `scripts/backend/test-admin-endpoints.{sh,ps1}` and `scripts/backend/test-oauth2-endpoints.{sh,ps1}` |
| Full cycle | `./manage.sh full-test` |
| Full stack (Docker) | `./manage.sh docker-up` / `./manage.sh docker-down` |

## Project Stats

- ~1.8万行 C++ 代码
- CMake + Conan 构建
- GitHub Actions CI/CD (Windows/Linux/macOS)
- OpenAPI 3.0 文档
- 多平台 CI: Linux (GCC + PostgreSQL + Redis), Windows (MSVC 2022, 内存存储), macOS (Clang ARM64)
