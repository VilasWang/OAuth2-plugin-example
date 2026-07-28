# CLAUDE.md

authforge — Production-grade OAuth2.0/OIDC authorization server (Drogon / C++17,
PostgreSQL, Redis, Vue.js frontends). Supports RFC 6749, 7662, 7009, 8414, 8628, 7591.

## Build & Test (non-standard flags only)

Standard build / run / test goes through the unified wrappers
`./manage.sh` / `./manage.ps1` — see `README.md` "Quick Start", or the
`/build-and-test` skill. Only the non-obvious flags are listed here:

- Debug build: append `-debug`.  Rebuild Drogon from source: `--build-drogon`.
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

## Critical Rules

Path-scoped hard constraints and workflow entry points live in `.claude/rules/`
and auto-load when you touch matching files:
- `orm-models.md` — never hand-edit `models/**`; regenerate with `/orm-gen`.
- `db-operations.md` — DB access is the async + Mapper + Criteria combo; raw SQL only for DDL / `UPDATE...RETURNING` / documented batch.
- `data-access.md` — pointer to `db-operations.md` for storage code.
- `dev-workflow.md` — dev commands: prefer `./manage.sh` / `./manage.ps1`; backend rebuild-DB / ORM / build / test / endpoint tests, frontend build / run / test.

`git push` is forbidden (human review required) and enforced as a deny rule in
`.claude/settings.json`.

Code-style and async/lambda/ORM conventions are in the `project-conventions`
skill (the single source for those). Highlights that matter in every edit:
never use `CoroMapper`; never capture `[this]` or `[&]` in async contexts (use
`shared_from_this()`); no emoji in code/output (use `[+]`/`[-]`/`[!]`).

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
