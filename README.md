# Drogon OAuth2.0 Provider — Full-Stack Authorization Server

[中文文档](README.zh-CN.md)

![Linux CI](https://github.com/lucaswang420/authforge/actions/workflows/ci-linux.yml/badge.svg)
![Windows CI](https://github.com/lucaswang420/authforge/actions/workflows/ci-windows.yml/badge.svg)
![macOS CI](https://github.com/lucaswang420/authforge/actions/workflows/ci-macos.yml/badge.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)

Production-grade OAuth2.0/OIDC authorization server with full support for RFC 6749, RFC 7662, RFC 7009, and RFC 8414. Includes admin console, user-facing frontend, and a comprehensive test suite.

---

## Architecture

```
authforge/
├── apps/server/        # Authorization server backend (Drogon C++ framework)
├── libs/               # SDK library packages (authforge::common/oauth2/identity/storage-*/drogon)
├── frontends/admin/    # Admin console frontend (Vue 3 + TailwindCSS)
├── frontends/user/     # User-facing frontend (Vue 3 + Pinia + TailwindCSS)
├── scripts/            # Build, test, and operations scripts
├── docs/               # Project documentation
└── PRD/                # Product design documents
```

### Tech Stack

| Layer | Technology |
|-------|------------|
| Backend Framework | Drogon (C++17) |
| Database | PostgreSQL 14+ |
| Cache | Redis 7+ |
| Admin Console | Vue 3 + Vite + Pinia + TailwindCSS |
| User Frontend | Vue 3 + Vite |
| Testing | CTest (C++) + Playwright (E2E) + PowerShell (API) |
| Monitoring | Prometheus + Audit Logging |
| Deployment | Docker Compose / Nginx |

---

## Features

### OAuth2/OIDC Core Protocols

| Feature | Standard | Endpoint |
|---------|----------|----------|
| Authorization Code + PKCE | RFC 6749 / RFC 7636 | `/oauth2/authorize`, `/oauth2/login`, `/oauth2/token` |
| Client Credentials | RFC 6749 | `/oauth2/token` (grant_type=client_credentials) |
| Token Refresh | RFC 6749 | `/oauth2/token` (grant_type=refresh_token) |
| Token Introspection | RFC 7662 | `/oauth2/introspect` |
| Token Revocation | RFC 7009 | `/oauth2/revoke` |
| OIDC Discovery | RFC 8414 | `/.well-known/openid-configuration` |
| JWKS | RFC 7517 | `/.well-known/jwks.json` |
| UserInfo | OIDC Core | `/oauth2/userinfo` |
| User Consent | OAuth2 | `/oauth2/consent` |
| Device Authorization | RFC 8628 | `/oauth2/device_authorization` |
| Dynamic Client Registration | RFC 7591 | `/oauth2/register` |

### User Authentication & Security

| Feature | Endpoint |
|---------|----------|
| User Registration | `POST /api/register` |
| Password Reset | `/api/password-reset/request`, `/api/password-reset/confirm` |
| Email Verification | `/api/verify-email`, `/api/verify-email/resend` |
| MFA (TOTP) | `/api/me/mfa/setup`, `/api/me/mfa/verify`, `/api/me/mfa/disable` |
| WebAuthn (FIDO2) | `/api/me/webauthn/register/*`, `/oauth2/webauthn/authenticate/*` |
| Google Login | `/api/google/login` |
| WeChat Login | `/api/wechat/login` |
| Account Lockout | Progressive lockout (5/10/15/20 failed attempts) |

### User Self-Service

| Feature | Endpoint |
|---------|----------|
| Profile | `GET /api/me` |
| Change Password | `PUT /api/me/password` |
| Authorized Apps | `GET/DELETE /api/me/authorized-apps` |
| Account Deletion | `DELETE /api/me` |

### Admin Console (frontends/admin)

| Module | Features |
|--------|----------|
| Dashboard | User count, app count, active tokens, failed login stats |
| App Management | Client CRUD, secret rotation, scope assignment, grant type config |
| User Management | User list/details, role assignment, disable/enable, lock status |
| Role Management | Role CRUD (protects built-in roles: admin/user) |
| Scope Management | Scope CRUD (protects built-in scopes: openid/profile/email/admin) |
| Token Management | Token listing, revocation by client/user, individual revocation |
| Organization Management | Multi-tenant organization CRUD |
| Audit Log | Paginated view, filter by event type/result |
| OIDC Keys | Signing key information |
| System Settings | Health monitoring |

### RBAC Permission System

- Role-based access control (admin / user / custom roles)
- URL pattern matching for permission checks (`/api/admin/.*` requires admin role)
- Triple-scope permission control (Client restriction + Role validation + Consent check)

### Observability

- Prometheus metrics export (`/metrics`)
- Structured audit logging (login, token issuance/revocation, password changes, etc.)
- Health check endpoints (`/health`, `/health/live`, `/health/ready`)

---

## Quick Start

### Docker Compose (Recommended)

```bash
docker-compose up -d
```

- User Frontend: `http://localhost:8080`
- Admin Console: `http://localhost:5174/admin/`
- Backend API: `http://localhost:5555`

### Local Development

```powershell
# 1. Build backend
.\manage.ps1 build-backend

# 2. Start backend (requires PostgreSQL + Redis)
cd apps\server
..\..\build\apps\server\Release\authforge-server.exe

# 3. Start admin console
cd frontends\admin
npm install
npm run dev    # http://localhost:5174/admin/

# 4. Start user frontend
cd frontends\user
npm install
npm run dev    # http://localhost:5173
```

### Default Credentials

| Username | Password | Role |
|----------|----------|------|
| admin | admin | admin |

---

## Testing

### Backend API Tests

```powershell
# Admin API full tests (37 tests)
.\scripts\backend\test-admin-endpoints.ps1

# OAuth2 core flow tests (17 tests)
.\scripts\backend\test-oauth2-endpoints.ps1
```

### Frontend E2E Tests

```powershell
cd frontends\admin
npx playwright test              # Full run (123 tests)
npx playwright test --ui         # UI mode for debugging
npx playwright test --headed     # Headed browser mode
```

### C++ Unit Tests

```powershell
cd build
ctest --output-on-failure
```

### Test Coverage

| Test Type | Count | Scope |
|-----------|-------|-------|
| Admin API (PowerShell) | 37 | All Admin endpoints + Organization |
| OAuth2 Core (PowerShell) | 17 | Auth flows, token management, user services |
| Frontend E2E (Playwright) | 123 | All admin console pages and interactions |
| C++ Unit Tests (CTest) | 111 | Core library logic |

---

## API Documentation

- **OpenAPI Spec**: [openapi.yaml](apps/server/openapi.yaml)
- **Swagger UI**: `http://localhost:5555/docs/api` (requires Swagger UI static files)
- **E2E Testing Guide**: [E2E_TESTING_GUIDE.md](docs/admin/e2e-testing-guide.md)

---

## Documentation

| Document | Description |
|----------|-------------|
| [Backend Configuration](docs/backend/configuration-guide.md) | Database, Redis, environment variable setup |
| [Security Architecture](docs/backend/security-architecture.md) | Token lifecycle, encryption, protection strategies |
| [RBAC Guide](docs/backend/rbac-guide.md) | Role permission configuration |
| [Docker Deployment](docs/backend/docker-deployment.md) | Containerized deployment |
| [CI/CD Pipeline](docs/backend/ci-cd-guide.md) | GitHub Actions configuration |
| [Admin Console Design](PRD/admin_console_design.md) | Admin console product design document |
| [Account Lockout](docs/ops/account-lockout.md) | Lockout rules and reset procedures |

---

## System Requirements

| Component | Minimum Version |
|-----------|-----------------|
| C++ Compiler | C++17 (MSVC 2019+ / GCC 9+ / Clang 10+) |
| CMake | 3.20+ |
| PostgreSQL | 14+ |
| Redis | 7+ |
| Node.js | 18+ |
| Docker | 24+ (optional) |

---

## License

MIT License — see [LICENSE](LICENSE)

---

**Project Status**: Production Ready | **Version**: v6.0.0
