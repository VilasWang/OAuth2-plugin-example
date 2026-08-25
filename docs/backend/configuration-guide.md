# Configuration & Deployment Guide

## 1. Environment Variable Injection

The application supports overriding critical configuration values using environment variables. This is essential for secure deployment in Docker/Kubernetes environments where secrets should not be hardcoded in `config.json`.

### Supported Environment Variables

| Variable Name | Description | Overrides Config Path | Example |
|---|---|---|---|
| `FULLA_DB_HOST` | Database Hostname | `db_clients[0].host` | `postgres` |
| `FULLA_DB_NAME` | Database Name | `db_clients[0].dbname` | `fulla_db` |
| `FULLA_DB_PASSWORD` | Database Password | `db_clients[0].passwd` | `secret` |
| `FULLA_REDIS_HOST` | Redis Hostname | `redis_clients[0].host` | `redis` |
| `FULLA_REDIS_PASSWORD` | Redis Password | `redis_clients[0].passwd` | `secret` |
| `FULLA_VUE_CLIENT_SECRET` | Vue Client Secret | `plugins[OAuth2Plugin].config.clients.vue-client.secret` | `...` |

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

- **fulla-frontend**: Vue SPA + Nginx (Builds from `deploy/docker/Dockerfile`, target `frontend-runtime`).
- **fulla-admin**: Admin console frontend (Builds from `frontends/admin/Dockerfile`).
- **fulla-backend**: The Drogon backend (Builds from `deploy/docker/Dockerfile`, target `backend-runtime`).
- **fulla-postgres**: PostgreSQL 15 (schema applied by the backend on startup via `FULLA_AUTO_MIGRATE=true`, reading `apps/server/migrations/`).
- **fulla-redis**: Redis 7 with password protection.
- **fulla-prometheus**: Metrics collection agent.

### Quick Start

```bash
# Build and Start (run from the repo root)
docker-compose -f deploy/docker/docker-compose.yml up -d --build

# Check Logs
docker-compose -f deploy/docker/docker-compose.yml logs -f fulla-backend

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

## 5. Client Token-Endpoint Auth Method (F-017)

Each client declares how it authenticates at `/oauth2/token`, `/oauth2/introspect`,
and `/oauth2/revoke` via the `oauth2_clients.token_endpoint_auth_method` column:

| Value | Semantics |
|---|---|
| `client_secret_basic` | Secret MUST travel in the `Authorization: Basic` header; body `client_secret` is rejected. |
| `client_secret_post` | Secret MUST travel in the POST body; a Basic header is rejected. |
| `none` | PUBLIC client; any `client_secret` is rejected. |
| NULL / empty | Legacy lenient fallback: Basic header is accepted, body secret is accepted (Basic→body fallback). |

Defaults applied at registration/admin-creation when the field is omitted:

- `PUBLIC` clients → `none` (they have no secret).
- `CONFIDENTIAL` clients → `client_secret_basic`.

The seed clients are explicit: `vue-client` and `admin-console` → `none`;
`backend-svc` → `client_secret_basic`. Existing clients with a NULL value preserve
the pre-Batch-2 behavior so deployments are not broken by the upgrade.

## 6. OIDC prompt / max_age / auth_time (F-022)

The authorization endpoint honors the OIDC Core §3.1.2.1 `prompt` and `max_age`
parameters:

- **`prompt=none`**: forbids any UI. With no session → 302 `error=login_required`;
  with consent required → `error=consent_required`. The error is redirected back to
  the verified `redirect_uri` with `state` echoed. Combining `none` with another
  value (e.g. `none login`) returns a direct 400 (self-contradictory).
- **`prompt=login`**: forces re-authentication even if a session exists.
- **`prompt=consent`**: forces the consent screen even if prior consent covers the
  requested scopes.
- **`max_age=<seconds>`**: if the session's `auth_time` (set at login / MFA verify)
  is older than `max_age`, re-authentication is forced.

`auth_time` and `amr` are persisted on the authorization code and stamped into the
id_token at exchange: `auth_time` (when >0), `amr` (JSON array when set), and `acr`
(`1` = password-only, `2` = MFA). The discovery document advertises
`prompt_values_supported`, `acr_values_supported`, and the claims.

## 7. RP-Initiated Logout (F-027) & Session Invalidation (F-028)

`/oauth2/end_session` (GET + POST) terminates the server-side session. To redirect
after logout, the client supplies a `post_logout_redirect_uri` that MUST be one of
the client's registered redirect URIs; the client is identified via the
`id_token_hint`'s `aud` claim (signature not verified per §2.2). Without a valid
hint + registered URI the request is rejected with 400; on success it redirects
(302) with `state` echoed, or returns 200 when no redirect URI was supplied.

`POST /oauth2/logout` (the existing API logout) additionally calls
`session()->clear()` (F-028), so the server-side session is terminated alongside
the access-token revocation.

## 8. Rate Limiting (F-018)

The token, introspect, revoke, and device-code-polling endpoints share a
process-wide, in-memory sliding-window rate limiter that buckets on
`(client_ip, client_id)`. After `max_failures` (default **30**) authentication /
validation failures within a rolling `window_seconds` (default **60**), subsequent
attempts return **HTTP 429** with a `Retry-After` header and an OAuth2-style
`{error, error_description}` body. Only **failures** are counted; a successful
request clears the bucket, so legitimate load (and the integration-test suite,
which makes many sequential successful requests) is never throttled.

Configure via `custom_config.auth.rate_limit` (all four `config*.json` files ship
the defaults explicitly):

```json
"custom_config": {
  "auth": {
    "require_pkce_for_public": true,
    "allow_http_redirect_uri": true,
    "rate_limit": {
      "max_failures": 30,
      "window_seconds": 60
    }
  }
}
```

Both keys are optional; if the `rate_limit` object is absent the built-in defaults
(30 / 60) apply. The limiter is a function-local singleton (`RateLimiter::instance()`
in `libs/common/include/fulla/common/utils/RateLimiter.h`), so all four
protected endpoints share one counter map per process. This is a minimal
brute-force / token-probing guard; for multi-instance deployments a shared
store (Redis) would be required (future work).

## 9. JWKS Key Rotation (F-029 — future ops task)

The JWKS endpoint (`/oauth2/.well-known/jwks.json`) currently serves a **single
static `kid`**, initialized once at plugin startup from the configured JWK
material (`OAuth2Plugin::initAndStart()` → `JwkManager::init()`). **Key rotation
is not implemented**: there is no rotation schedule, no kid-rollover window, and
no dual-key publication. Tokens signed by the current key validate for their
full lifetime; rotating the key invalidates all outstanding tokens signed by the
predecessor.

Production rotation is tracked as a future ops task. Until then, operators
treating key compromise as in-scope should restart the server with new JWK
material (and accept that all previously-issued tokens become invalid).
