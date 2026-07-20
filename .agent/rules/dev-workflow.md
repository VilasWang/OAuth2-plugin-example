---
description: Dev workflow entry points — prefer manage.sh / manage.ps1; skills as explainers
globs:
  - "OAuth2Server/**"
  - "OAuth2Admin/**"
  - "OAuth2Frontend/**"
---

Prefer the project's unified wrappers `./manage.sh` (Linux/macOS) or
`./manage.ps1` (Windows) for any backend task — they encode the same path the CI
uses. The `/build-and-test`, `/orm-gen`, `/db-reset`, `/e2e-test` skills are
detailed explainers; call the wrapper first, drop into a skill when you need
the steps spelled out.

## Backend

| Step | Run |
|------|-----|
| Rebuild database | `/db-reset` skill (drops + recreates the PG DB via `psql`; no manage subcommand exists) |
| Regenerate ORM models | `./manage.sh generate-models` (or `./manage.ps1 generate-models`) — the `/orm-gen` skill wraps this |
| Build | `./manage.sh build-backend` (`-debug` for Debug) |
| Run server | `./manage.sh run-backend` (`-debug`) — direct: `build/OAuth2Server/{Debug|Release}/OAuth2Server -c config.json` |
| Unit / integration tests | `./manage.sh test-backend` (`-debug`). By label: `ctest -R Unit\|Integration\|E2E\|Security\|Performance`. No-DB test build: `-DOAUTH2_MEMORY_TESTS_ONLY=ON`. |
| Endpoint API tests | `scripts/backend/test-admin-endpoints.{sh,ps1}` (admin, 37 tests) and `scripts/backend/test-oauth2-endpoints.{sh,ps1}` (OAuth2 core, 17 tests) |
| Full cycle | `./manage.sh full-test` (build + unit + API tests) |

## Frontend (run inside the respective dir, e.g. `cd OAuth2Admin`)

| Step | Admin (`OAuth2Admin`) / User (`OAuth2Frontend`) |
|------|----|
| Install (once) | `npm install` |
| Run dev server | `npm run dev` (admin → `localhost:5174/admin/`; user → `localhost:5173`) |
| Unit tests | `npm run test:unit` |
| E2E tests | `npx playwright test` |
| Production build | `npm run build` |

## Full stack (Docker)

`./manage.sh docker-up` (frontend :8080, admin :5174, backend API :5555) /
`./manage.sh docker-down`. Use the `/docker-integration-test` or `/e2e-test`
skill for guided integration checks.

Standard build/run/test flags also live in `README.md` "Quick Start".
