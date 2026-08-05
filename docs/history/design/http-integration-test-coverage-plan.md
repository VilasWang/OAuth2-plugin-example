# HTTP Integration Test Coverage Push — Plan

> Branch: `test/coverage-push`. Target: overall coverage 48.5% → **80%** (Linux-CI
> leg, where Postgres+Redis run). Windows/macOS legs stay memory-only.
> Status: **approved** (2026-08-05), implementation in progress.

## Research basis

Four research workstreams, all verified against source:
1. Drogon HTTP-test patterns (local copy `D:\work\development\Repos\cpp\drogon-ecosystem\drogon`).
2. Project test-harness inventory (`tests/test_main.cc`, CMake, existing HTTP tests).
3. Admin-auth investigation (AuthorizationFilter, OAuth2Plugin, login recipe, seed SQL).
4. Contract-fixture pattern (`tests/contract/ContractFixtures.h`).
Plus a code-review sub-agent pass that found two blocking issues (B1, B2 below),
both verified against source and folded into this plan.

## Verified facts the design rests on

- All 5 admin services call `drogon::app().getDbClient()` directly (no memory path)
  → admin coverage needs Postgres.
- Admin auth = 2-step OAuth2 auth-code grant: `POST /oauth2/login` (form, with
  `json=true`) → `POST /oauth2/token` (form) → `Authorization: Bearer <token>` on
  `/api/admin/*`. Verified in `SessionController.cc:612`, `TokenEndpointController.cc:603`,
  `scripts/backend/common-test-functions.ps1` Get-AdminToken.
- `SchemaManager::migrate(db, migrationsDir)` is a static method already compiled
  into the test binary (`tests/CMakeLists.txt:40`) → in-process seeding feasible.
- `tests/test_main.cc` boots the full app on a background thread (port 5555),
  registers all admin controllers (`:333-356`), loads `config.json` with
  `rbac_rules` (`:273`). `test::run()` runs on the **main thread** (`:437`) →
  the synchronous `HttpClient::sendRequest(req, timeout)` overload is safe.
- `AuthorizationFilter` is default-deny, reads `rbac_rules` from custom config at
  runtime; `/api/admin/.*` requires role `admin`. No wiring changes needed.

## Blocking issues found in review (resolved in Phase 1)

- **B1:** The test binary does NOT seed the admin user. `SchemaSetup.cc` only
  creates a bare `users` table; the admin user comes from
  `apps/server/seed/dev_admin_user.sql`, applied only by the Linux CI shell step.
  **Fix:** in-process seeder in Phase 1.
- **B2:** Windows/macOS CI runners can't run Postgres/Redis via Docker
  (`_build-test.yml:210-213` explicit comment; `ci.yml:104,119` use_database=false).
  **Fix:** scope DB-backed coverage to Linux CI; 80% is a Linux-leg target.

Plus four important fixes: `json=true` is a query/form param not a JSON body field
(I1); test names must match `Integration_P[0-3]_*` (I2 — CI fast-gate); admin CRUD
needs unique IDs + RAII cleanup guards (I3 — no rollback pattern exists); commit to
a branch-coverage matrix per endpoint (I4).

## Phases

### Phase 1 — Foundation
1. `tests/common/HttpTestClient.h` — shared HTTP helper (kTestBaseUrl, serverReachable,
   send{Get,PostForm,PostJson,PutJson,Delete}, parseJsonBody, loginAsAdmin,
   authed* wrappers). Model on ContractFixtures.h.
2. `tests/common/StorageSeed.h` + extend `tests/SchemaSetup.cc` — in-process seeder:
   when not memory, run SchemaManager::migrate + seed SQL + admin lockout reset.
3. Repoint dead `127.0.0.1:8080` refs.
**Exit:** one admin smoke test passes under Postgres, skips cleanly under memory.

### Phase 2 — Admin coverage (~3287 LOC, 0% → ~70%)
4-8. One file per admin domain (Client/User/RoleScope/Token/Audit), each a
`DROGON_TEST` per branch (happy + 400 + 404 + 409 + 401/403), unique IDs + RAII cleanup.

### Phase 3 — Controller happy+error paths
9-14. Health (memory-safe), Discovery (memory-safe), SessionLogin, UserSelfService,
PasswordReset, EmailVerification, Mfa.

### Phase 4 — Storage depth branches (Postgres/Redis)
15-17. New `tests/contract/` files using ContractFixtures.h, append to
`OAUTH2_CONTRACT_TEST_NAMES`.

### Phase 5 — Wire CTest + CI, measure, decide
18-20. Document GLOB reconfigure need, append Contract names, ensure Linux gcovr
aggregates new coverage, rebuild + measure vs 80%.

## Out of scope
- Refactoring admin services to be memory-capable.
- Windows/macOS Postgres legs (infeasible).
- Frontend, perf/e2e expansion, last-15% error-branch chase (deferred to Phase 6).
