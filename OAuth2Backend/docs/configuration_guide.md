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

1. **Loader Hook**: At startup, `main.cc` calls `loadConfigWithEnv()`.
2. **Parsing**: It reads the base `config.json`.
3. **Injection**: It checks for the existence of the above environment variables. If found, it updates the JSON object in memory.
4. **Runtime File**: It writes the modified configuration to a temporary file `config_env_runtime.json`.
5. **Load**: Drogon loads this runtime configuration file.

### Verification

A dedicated test `EnvInjectionVerify` (in `EnvConfigTest.cc`) ensures that this logic works correctly.

## 2. Docker Deployment

The project includes a `docker-compose.yml` for orchestrating the full stack.

### Service Stack

- **oauth2-server**: The Drogon backend (Builds from `Dockerfile`).
- **postgres**: PostgreSQL 15 (Auto-initialized via `sql/` scripts).
- **redis**: Redis with password protection.
- **prometheus**: Metrics collection agent.

### Quick Start

```bash
# Build and Start
docker-compose up -d --build

# Check Logs
docker-compose logs -f oauth2-server

# Stop
docker-compose down
```

### Config Handling in Docker

The `Dockerfile` copies `config.json` to the container. The `docker-compose.yml` injects the environment variables defined in the `environment` section, effectively overriding the file-based defaults at runtime.
