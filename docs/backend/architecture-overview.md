# OAuth2 System Architecture Overview

This document summarizes the Drogon OAuth2 plugin example from the service,
storage, request-flow, and deployment perspectives.

## 1. Technology Stack

| Layer | Technology | Purpose |
|---|---|---|
| Web framework | Drogon | High-performance asynchronous C++ HTTP service |
| Primary database | PostgreSQL 15 | Users, roles, clients, auth codes, and tokens |
| Cache / KV store | Redis | Optional L2 cache (client + access-token reads) in front of Postgres; standalone Redis storage is deprecated (rate limiting is process-local, not Redis) |
| Frontend | Vue 3 + Vite | SPA client for OAuth2 authorization-code flow |
| Deployment | Docker Compose | Local and production-like full-stack deployment |
| Observability | Prometheus | Metrics collection through Drogon PromExporter |

## 2. Module Layout

The project has been refactored into a core Plugin Library and a Demo Server to ensure true pluggability.

```text
HTTP request
  |
  |-- authforge-server (apps/server — demo server binary)
  |   `-- AuthService: local app authentication (libs/drogon/src/AuthService.cc)
  |
  `-- authforge::drogon (libs/drogon — standalone SDK package, target authforge::drogon)
      |-- Plugin Core
      |   `-- OAuth2Plugin: initialization and lifecycle manager
      |
      |-- Protocol Controllers & Filters (Auto-registered)
      |   |-- AuthorizationEndpointController / TokenEndpointController / DiscoveryController:
      |   |       handles /oauth2/authorize, /token, /.well-known/*, /userinfo
      |   `-- Filters: AuthorizationFilter, OAuth2AuthFilter
      |
      |-- Service Layer (Core Business Logic, libs/oauth2)
      |   |-- TokenService: PKCE, code/token generation and exchange
      |   |-- ClientService: client credentials and redirect URI validation
      |   `-- IdentityService: RBAC, subject mapping, and user consent
      |
      `-- Storage Layer (per-repository ports + bundle per backend)
          |-- I{Client,Grant,Token,Consent,UserInfo}Repository: storage ports (libs/oauth2)
          |-- MemoryRepositoryBundle: in-process test storage (libs/storage-memory)
          |-- PostgresRepositoryBundle: persistent storage (libs/storage-postgres)
          `-- RedisRepositoryBundle: Redis storage (libs/storage-redis)
```

## 3. Authorization-Code Flow

```text
Vue SPA                 authforge-server (App)     OAuth2Plugin (Core)        Storage
  |                          |                            |                     |
  | GET /oauth2/authorize    |                            |                     |
  |------------------------------------------------------>| validate client     |
  |                          |                            |-------------------->|
  |                          |                            |<--------------------|
  |<------------------------------------------------------| 302 to App /login   |
  |                          |                            |                     |
  | POST /api/login          |                            |                     |
  |------------------------->| AuthService::validateUser  |                     |
  |                          |------------------------------------------------->|
  |                          |<-------------------------------------------------|
  |<-------------------------| 302 /callback?code=...     |                     |
  |                          |                            |                     |
  | POST /oauth2/token       |                            |                     |
  |------------------------------------------------------>| consume auth code   |
  |                          |                            |-------------------->|
  |                          |                            | save access token   |
  |                          |                            |-------------------->|
  |<------------------------------------------------------| access_token JSON   |
  |                          |                            |                     |
  | GET /oauth2/userinfo     |                            |                     |
  |------------------------------------------------------>| validate token      |
  |                          |                            |-------------------->|
  |                          |                            |<--------------------|
  |<------------------------------------------------------| user info JSON      |
```

## 4. Storage Strategy

`OAuth2Plugin` selects the backend through `storage_type` in `config.json`.

| storage_type | Implementation | Typical use |
|---|---|---|
| memory | `MemoryRepositoryBundle` | Unit tests and quick local demos |
| redis | `RedisRepositoryBundle` | **DEPRECATED** — logs ERROR at startup and rejects the `refresh_token` grant with `unsupported_grant_type`; do not use. Redis now serves only as an optional cache layer in front of Postgres (see [Configuration Guide §3](configuration-guide.md)). |
| postgres | `PostgresRepositoryBundle` | Durable production storage |

> 每个 `*RepositoryBundle` 同时装配同一后端下的 client / grant / token / consent / userinfo 五个仓储实现（见各 `libs/storage-*/include` 下的头文件）。

## 5. Frontend and Backend Integration

The frontend starts the OAuth2 authorization-code flow from the login page,
stores the CSRF `state` in localStorage, handles `/callback`, exchanges the
code for tokens, and then calls `/oauth2/userinfo`.

For third-party providers, the frontend receives the external authorization
code and sends it to the backend endpoints:

- `/api/google/login`
- `/api/wechat/login`

The backend performs the provider token exchange server-side so provider
secrets are not exposed to the browser.

## 6. Deployment Notes

The provided Docker Compose stack starts:

- `oauth2-frontend` on port `8080`
- `oauth2-backend` on port `5555`
- PostgreSQL on host port `5433`
- Redis on host port `6380`
- Prometheus on port `9090`

In production, terminate TLS at a reverse proxy and proxy API requests to the
Drogon backend.
