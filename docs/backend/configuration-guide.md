# Configuration & Deployment Guide

## 1. Environment Variable Injection

The application supports overriding critical configuration values using environment variables. This is essential for secure deployment in Docker/Kubernetes environments where secrets should not be hardcoded in `config.json`.

### Supported Environment Variables

| Variable Name | Description | Overrides Config Path | Example |
|---|---|---|---|
| `OAUTH2_DB_HOST` | Database Hostname | `db_clients[0].host` | `postgres` |
| `OAUTH2_DB_NAME` | Database Name | `db_clients[0].dbname` | `oauth2_db` |
| `OAUTH2_DB_PASSWORD` | Database Password | `db_clients[0].passwd` | `secret` |
| `OAUTH2_REDIS_HOST` | Redis Hostname | `redis_clients[0].host` | `redis` |
| `OAUTH2_REDIS_PASSWORD` | Redis Password | `redis_clients[0].passwd` | `secret` |
| `OAUTH2_VUE_CLIENT_SECRET` | Vue Client Secret | `plugins[OAuth2Plugin].config.clients.vue-client.secret` | `...` |

### How It Works

1. **Loader Hook**: At startup, `main.cc`'s `loadConfiguration()` helper calls `common::config::ConfigManager::load()` then `ConfigManager::validate()`.
2. **Parsing**: It reads the base `config.json` into a `Json::Value` object.
3. **Injection**: It checks for the existence of the supported environment variables. If found, it updates the corresponding nodes in the `Json::Value` object in memory.
4. **Load**: Drogon directly loads this modified configuration object using `drogon::app().loadConfigJson(config)`. No temporary files are created on disk.

### Verification

A dedicated test `EnvInjectionVerify` (in `EnvConfigTest.cc`) ensures that this logic works correctly.

## 2. Docker Deployment

The project includes a `docker-compose.yml` for orchestrating the full stack.

### Service Stack

- **oauth2-frontend**: Vue SPA + Nginx (Builds from `deploy/docker/Dockerfile`, target `frontend-runtime`).
- **oauth2-admin**: Admin console frontend (Builds from `frontends/admin/Dockerfile`).
- **oauth2-backend**: The Drogon backend (Builds from `deploy/docker/Dockerfile`, target `backend-runtime`).
- **oauth2-postgres**: PostgreSQL 15 (schema applied by the backend on startup via `OAUTH2_AUTO_MIGRATE=true`, reading `apps/server/migrations/`).
- **oauth2-redis**: Redis 7 with password protection.
- **oauth2-prometheus**: Metrics collection agent.

### Quick Start

```bash
# Build and Start (run from the repo root)
docker-compose -f deploy/docker/docker-compose.yml up -d --build

# Check Logs
docker-compose -f deploy/docker/docker-compose.yml logs -f oauth2-backend

# Stop
docker-compose -f deploy/docker/docker-compose.yml down
```

### Config Handling in Docker

`docker-compose.yml` mounts `apps/server/config/config.json` into the container read-only. The `environment` section injects the environment variables (see §1), which override the file-based defaults at runtime via `ConfigManager::load()` + env injection.

## 3. Storage Backend Selection

The OAuth2 plugin's `config.storage_type` selects the persistence backend:

| `storage_type` | Status | Notes |
|---|---|---|
| `postgres` | **Supported (the only production backend)** | Full token persistence, refresh-token rotation, reuse detection. |
| `redis` | **DEPRECATED** | Historically never persisted refresh tokens (`saveRefreshToken`/`getRefreshToken` were no-ops), so rotation and reuse-detection were silently non-functional. The mode still boots for backward compatibility and logs an ERROR at startup, but the `refresh_token` grant is rejected with `unsupported_grant_type`. Do not use for new deployments. |
| `memory` | Testing only | Intended for unit/integration tests, not production. |

Target architecture: **Postgres as the storage layer, with Redis returning later as a cache layer in front of Postgres** (tracked as a separate architecture issue; no standalone Redis storage mode will be revived).

## 4. Issuer Configuration

`config.metadata.issuer` (custom config) is the single source of truth for the server's issuer URL. It is read once at startup by `OAuth2Plugin` and used consistently for:

- the `iss` claim stamped on access tokens at issuance time (authorization_code, refresh_token, client_credentials, device_code grants),
- the introspection response `iss` (backfilled from the configured issuer when a stored row carries none),
- the discovery documents (`/.well-known/openid-configuration`, `/.well-known/oauth-authorization-server`).

Constraints:

- A trailing slash is normalized away automatically; do not rely on it.
- Defaults to `http://localhost:5555` when unset; a `LOG_WARN` is emitted in that case.
- Production deployments **MUST** configure an `https://` issuer; a plain-`http` issuer on a non-loopback host logs a startup warning.
- The introspection `iss` and the discovery `issuer` are guaranteed byte-identical (OIDC Discovery §3 requirement).
