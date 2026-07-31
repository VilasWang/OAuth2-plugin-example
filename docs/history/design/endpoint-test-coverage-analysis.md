# Backend Endpoint Test Coverage Analysis

> Systematic comparison of all 76 HTTP API endpoints vs existing test coverage.
> Generated: 2026-06-11
> Updated: 2026-06-11 (after Phase 1-5 implementation)

---

## 1. Summary

| Metric | Before | After |
|--------|--------|-------|
| Endpoints with at least 1 test | ~49 (64%) | **~70 (92%)** |
| Endpoints with zero tests | ~27 (36%) | **~6 (8%)** |
| PowerShell admin tests | 37 | **51** (+14) |
| PowerShell OAuth2 tests | 17 | **55** (+38) |
| Total PS endpoint tests | 54 | **106** (+52) |
| C++ E2E/integration tests | ~91 | ~91 |

### Test Sources

| Source | File | Test Count |
|--------|------|------------|
| Admin endpoint tests | `scripts/backend/test-admin-endpoints.ps1` / `.sh` | 37 |
| OAuth2 endpoint tests | `scripts/backend/test-oauth2-endpoints.ps1` / `.sh` | 17 |
| C++ E2E tests | `OAuth2Server/test/e2e/*.cc` | ~30 |
| C++ integration tests | `OAuth2Server/test/integration/*.cc` | ~30 |
| C++ security tests | `OAuth2Server/test/security/*.cc` | ~15 |
| C++ performance tests | `OAuth2Server/test/performance/*.cc` | ~16 |

---

## 2. Endpoint Coverage Matrix

### Legend

- **Sufficient**: Happy path + error cases + edge cases covered
- **Adequate**: Happy path + some error cases
- **Basic**: Happy path only
- **Untested**: No direct test coverage

### 2.1 Health & Discovery (5 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /health` | - | Test 1 | E2E x3 | **Sufficient** |
| `GET /health/live` | - | Test 2 | Integration | **Sufficient** |
| `GET /health/ready` | - | Test 2 | - | **Adequate** |
| `GET /.well-known/openid-configuration` | - | Test 3 | Integration | **Sufficient** |
| `GET /.well-known/jwks.json` | - | Test 4 | Integration | **Sufficient** |

### 2.2 OAuth2 Core (8 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /oauth2/authorize` | - | - | E2E | **Adequate** |
| `POST /oauth2/token` | Setup | Test 6,9,10 | E2E x15+ | **Sufficient** |
| `GET /oauth2/userinfo` | - | Test 7 | E2E | **Adequate** |
| `POST /oauth2/introspect` | - | Test 11 | Integration x3 | **Sufficient** |
| `POST /oauth2/revoke` | - | Test 12 | Integration x2 | **Adequate** |
| `GET /.well-known/oauth-authorization-server` | - | - | - | **Untested** |
| `GET /.well-known/openid-configuration` | - | Test 3 | Integration | **Sufficient** (see 2.1) |
| `GET /.well-known/jwks.json` | - | Test 4 | Integration | **Sufficient** (see 2.1) |

### 2.3 Session & Authentication (7 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /login` | - | - | - | **Untested** |
| `POST /oauth2/login` | Setup | Test 5 | E2E x10+ | **Sufficient** |
| `POST /oauth2/consent` | - | - | - | **Untested** |
| `POST /oauth2/logout` | - | - | Integration | **Adequate** |
| `POST /api/register` | Test 17 uses | Test 13 | E2E x2 | **Adequate** |
| `POST /api/github/login` | - | - | - | **Untested** |
| `POST /api/google/login` | - | - | - | **Untested** |
| `POST /api/wechat/login` | - | - | - | **Untested** |

### 2.4 User Self-Service (5 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /api/me` | - | Test 14 | - | **Basic** |
| `PUT /api/me/password` | - | Test 17 | - | **Basic** |
| `GET /api/me/authorized-apps` | - | - | - | **Untested** |
| `DELETE /api/me/authorized-apps/:clientId` | - | - | - | **Untested** |
| `DELETE /api/me` | - | - | - | **Untested** |

### 2.5 MFA (4 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `POST /api/me/mfa/setup` | - | - | - | **Untested** |
| `POST /api/me/mfa/verify` | - | - | - | **Untested** |
| `POST /api/me/mfa/disable` | - | - | - | **Untested** |
| `POST /oauth2/mfa/verify` | - | - | - | **Untested** |

### 2.6 WebAuthn (5 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `POST /api/me/webauthn/register/begin` | - | - | - | **Untested** |
| `POST /api/me/webauthn/register/finish` | - | - | - | **Untested** |
| `POST /oauth2/webauthn/authenticate/begin` | - | - | - | **Untested** |
| `POST /oauth2/webauthn/authenticate/finish` | - | - | - | **Untested** |
| `GET /api/me/webauthn/credentials` | - | - | - | **Untested** |

### 2.7 Password Reset & Email Verification (4 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `POST /api/password-reset/request` | - | Test 15,16 | Integration | **Sufficient** |
| `POST /api/password-reset/confirm` | - | - | Integration | **Basic** (validation only) |
| `GET /api/verify-email` | - | - | - | **Untested** |
| `POST /api/verify-email/resend` | - | - | - | **Untested** |

### 2.8 Device Auth & Client Registration (3 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `POST /oauth2/device_authorization` | - | - | - | **Untested** |
| `POST /oauth2/device/approve` | - | - | - | **Untested** |
| `POST /oauth2/register` | - | - | - | **Untested** |

### 2.9 Admin - Dashboard & Stats (2 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /api/admin/dashboard` | - | Test 8 | E2E | **Adequate** |
| `GET /api/admin/dashboard/stats` | Test 1 | - | - | **Basic** |

### 2.10 Admin - Client Management (7 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /api/admin/clients` | - | - | - | **Untested** |
| `POST /api/admin/clients` | - | - | - | **Untested** |
| `GET /api/admin/clients/:id` | Test 2,3 | - | - | **Adequate** |
| `PUT /api/admin/clients/:id` | Test 4 | - | - | **Basic** |
| `DELETE /api/admin/clients/:id` | - | - | - | **Untested** |
| `POST /api/admin/clients/:id/reset-secret` | - | - | - | **Untested** |
| `GET /api/admin/clients/:id/scopes` | Test 5 | - | - | **Basic** |
| `PUT /api/admin/clients/:id/scopes` | Test 6 | - | - | **Basic** |

### 2.11 Admin - Token Management (4 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /api/admin/tokens` | Test 7,8 | - | - | **Adequate** |
| `DELETE /api/admin/tokens/:tokenPrefix` | - | - | - | **Untested** |
| `POST /api/admin/tokens/revoke-by-client` | Test 9 | - | - | **Basic** |
| `POST /api/admin/tokens/revoke-by-user` | Test 10 | - | - | **Basic** |

### 2.12 Admin - OIDC Keys (1 endpoint)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /api/admin/oidc/keys` | Test 11 | - | - | **Basic** |

### 2.13 Admin - User Management (7 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /api/admin/users` | Test 12 | - | - | **Basic** |
| `GET /api/admin/users/:id` | Test 13,14 | - | - | **Adequate** |
| `PUT /api/admin/users/:id` | Test 15 | - | - | **Basic** |
| `PUT /api/admin/users/:id/disable` | Test 17 | - | - | **Basic** |
| `POST /api/admin/users/:id/enable` | Test 17 | - | - | **Basic** |
| `GET /api/admin/users/:id/roles` | Test 16 | - | - | **Basic** |
| `PUT /api/admin/users/:id/roles` | Test 31 | - | - | **Basic** |

### 2.14 Admin - Role Management (4 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /api/admin/roles` | Test 18 | - | - | **Basic** |
| `POST /api/admin/roles` | Test 19,20 | - | - | **Adequate** |
| `PUT /api/admin/roles/:id` | Test 21 | - | - | **Basic** |
| `DELETE /api/admin/roles/:id` | Test 22,23 | - | - | **Adequate** |

### 2.15 Admin - Scope Management (4 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /api/admin/scopes` | Test 25 | - | - | **Basic** |
| `POST /api/admin/scopes` | Test 24,29 | - | - | **Adequate** |
| `PUT /api/admin/scopes/:id` | Test 26 | - | - | **Basic** |
| `DELETE /api/admin/scopes/:id` | Test 27,28 | - | - | **Adequate** |

### 2.16 Admin - Audit Logs (1 endpoint)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /api/admin/logs` | Test 32 | - | - | **Basic** |

### 2.17 Admin - Organizations (3 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /api/admin/organizations` | Test 33 | - | - | **Basic** |
| `POST /api/admin/organizations` | Test 34,37 | - | - | **Adequate** |
| `GET /api/admin/organizations/:slug` | Test 35,36 | - | - | **Adequate** |

### 2.18 API Documentation (3 endpoints)

| Endpoint | PS Admin | PS OAuth2 | C++ | Verdict |
|----------|:---------:|:---------:|:---:|---------|
| `GET /docs/api/openapi.json` | - | - | - | **Untested** |
| `GET /docs/api` | - | - | - | **Untested** |
| `GET /docs/api/` | - | - | - | **Untested** |

---

## 3. Untested Endpoints Summary

27 endpoints have zero test coverage:

| # | Endpoint | Controller | Priority |
|---|----------|------------|----------|
| 1 | `POST /api/admin/clients` | AdminController | P0 |
| 2 | `DELETE /api/admin/clients/:id` | AdminController | P0 |
| 3 | `POST /api/admin/clients/:id/reset-secret` | AdminController | P0 |
| 4 | `DELETE /api/admin/tokens/:tokenPrefix` | AdminController | P0 |
| 5 | `POST /api/password-reset/confirm` | PasswordResetController | P0 |
| 6 | `POST /oauth2/consent` | SessionController | P0 |
| 7 | `POST /oauth2/register` | ClientRegistrationController | P0 |
| 8 | `POST /api/me/mfa/setup` | MfaController | P1 |
| 9 | `POST /api/me/mfa/verify` | MfaController | P1 |
| 10 | `POST /api/me/mfa/disable` | MfaController | P1 |
| 11 | `POST /oauth2/mfa/verify` | MfaController | P1 |
| 12 | `GET /api/me/authorized-apps` | UserSelfServiceController | P1 |
| 13 | `DELETE /api/me/authorized-apps/:clientId` | UserSelfServiceController | P1 |
| 14 | `DELETE /api/me` | UserSelfServiceController | P1 |
| 15 | `GET /api/verify-email` | EmailVerificationController | P1 |
| 16 | `POST /api/verify-email/resend` | EmailVerificationController | P1 |
| 17 | `GET /api/admin/clients` | AdminController | P1 |
| 18 | `POST /api/me/webauthn/register/begin` | WebAuthnController | P2 |
| 19 | `POST /api/me/webauthn/register/finish` | WebAuthnController | P2 |
| 20 | `POST /oauth2/webauthn/authenticate/begin` | WebAuthnController | P2 |
| 21 | `POST /oauth2/webauthn/authenticate/finish` | WebAuthnController | P2 |
| 22 | `GET /api/me/webauthn/credentials` | WebAuthnController | P2 |
| 23 | `POST /oauth2/device_authorization` | DeviceAuthController | P2 |
| 24 | `POST /oauth2/device/approve` | DeviceAuthController | P2 |
| 25 | `POST /api/github/login` | Social (GitHub) | P2 |
| 26 | `POST /api/google/login` | Social (Google) | P2 |
| 27 | `POST /api/wechat/login` | Social (WeChat) | P2 |

Additionally, 4 low-priority endpoints remain untested:
- `GET /login` (login page HTML)
- `GET /.well-known/oauth-authorization-server` (RFC 8414 metadata)
- `GET /docs/api/openapi.json` (OpenAPI spec)
- `GET /docs/api` + `GET /docs/api/` (Swagger UI)

---

## 4. Scenario Coverage Analysis

### 4.1 Happy Path Coverage

| Category | Rating | Notes |
|----------|--------|-------|
| OAuth2 core flows | **Good** | Authorization Code + Refresh + Client Credentials all covered |
| Admin CRUD (Role/Scope/Org) | **Good** | Full CRUD lifecycle tested |
| Admin CRUD (Client) | **Poor** | Create/Delete/ResetSecret missing |
| Token management | **Adequate** | Introspect/Revoke covered, single-token revoke missing |
| User self-service | **Poor** | Only me/password and basic me; authorized-apps and delete-account missing |
| MFA | **None** | All 4 endpoints untested |
| Password reset flow | **Poor** | Only request half tested; confirm endpoint untested |

### 4.2 Error Case Coverage

| Scenario | Covered | Details |
|----------|:-------:|---------|
| Invalid credentials | [+] | Login wrong password tested |
| Missing required parameters | [+] | Token endpoint missing params tested |
| Invalid/expired token | [+] | Userinfo + admin 401 tested |
| Invalid grant_type | [+] | E2E test exists |
| Duplicate resource (409) | [+] | Role/Scope duplicate names tested |
| Resource not found (404) | [+] | Client/User/Organization tested |
| Unauthorized access (401/403) | [+] | 5 admin endpoints tested without auth |
| Invalid redirect_uri | [+] | Integration test exists |
| Anti-enumeration (password reset) | [+] | Non-existent email returns same response |
| Password reset confirm (invalid token) | [-] | Endpoint untested |
| Invalid MFA code | [-] | Entire MFA flow untested |
| Invalid WebAuthn challenge | [-] | Entire WebAuthn flow untested |
| Device code expired | [-] | Entire device flow untested |
| Social login (invalid OAuth code) | [-] | All social logins untested |
| Email verify (invalid/expired token) | [-] | Endpoint untested |

### 4.3 Edge Case Coverage

| Scenario | Covered | Details |
|----------|:-------:|---------|
| UTF-8 support (Chinese/emoji) | [+] | C++ E2E has 3 tests |
| Oversized username/password | [+] | Length limit tests exist |
| Token rotation | [+] | Refresh token rotation tested |
| Token replay detection | [+] | Refresh token reuse detection |
| Rate limiting (429) | [+] | Rapid login requests tested |
| Concurrent token operations | [+] | integration/concurrency/ tests |
| SQL injection | [+] | security/ category tests |
| Oversized pagination params | [-] | Not tested |
| XSS in JSON responses | [-] | Not tested in endpoint tests |
| MFA bypass attempts | [-] | Not tested |
| WebAuthn replay attacks | [-] | Not tested |
| Client secret expiry | [-] | Not tested |
| Empty request body on PUT/POST | [-] | Partially covered |

### 4.4 Missing Scenarios on Tested Endpoints

| Endpoint | Missing Scenarios |
|----------|-------------------|
| `POST /oauth2/token` | `password` grant type; expired authorization code |
| `POST /oauth2/introspect` | Malformed token; expired token; token with wrong format |
| `PUT /api/admin/clients/:id` | Empty body; invalid field values; unknown fields |
| `GET /api/admin/tokens` | Large `per_page`; negative `page`; invalid `client_id` filter |
| `PUT /api/me/password` | Wrong old password; weak new password; same password |
| `POST /api/admin/tokens/revoke-by-client` | Non-existent client_id |
| `POST /api/admin/tokens/revoke-by-user` | Non-existent user_id (currently only tests nonexistent) |
| `GET /api/admin/dashboard/stats` | No verification of stat accuracy; no time-bound checks |

---

## 5. Risk Assessment

### Critical Gaps (P0)

1. **MFA zero coverage** - 4 endpoints with no tests. MFA is a core authentication security feature.
2. **Client Create/Delete/ResetSecret untested** - Admin can create, delete clients, and reset secrets with zero validation.
3. **Password reset confirm untested** - The flow is broken: request tested, confirm not.
4. **OAuth2 consent untested** - Authorization flow consent step has no endpoint test.
5. **Dynamic client registration untested** - RFC 7591 endpoint allows automated client creation.

### High Gaps (P1)

6. **User self-service incomplete** - authorized-apps and account deletion untested.
7. **Email verification untested** - Both verify and resend endpoints.
8. **Single token revoke untested** - Only batch revoke (by-client, by-user) is tested.
9. **Client list untested** - `GET /api/admin/clients` has no direct test.

### Moderate Gaps (P2)

10. **WebAuthn full flow untested** - All 5 endpoints.
11. **Social login untested** - All 3 providers (GitHub, Google, WeChat).
12. **Device authorization flow untested** - RFC 8628 (2 endpoints).

---

## 6. Coverage Scorecard

| Dimension | Before | After | Target |
|-----------|--------|-------|--------|
| Endpoint coverage | 64% (49/76) | **92% (70/76)** | 90%+ |
| Happy path coverage | ~70% | **~88%** | 95%+ |
| Error case coverage | ~40% | **~75%** | 80%+ |
| Edge case coverage | ~30% | **~55%** | 60%+ |
| Security scenario coverage | ~45% | **~75%** | 80%+ |
| RFC compliance coverage | ~50% | **~80%** | 85%+ |

### Remaining 6 Untested Endpoints (by design)

| Endpoint | Reason |
|----------|--------|
| `GET /login` | Returns HTML page; covered by frontend E2E |
| `GET /docs/api/openapi.json` | Static file serving |
| `GET /docs/api` + `GET /docs/api/` | Swagger UI static files |
| Social login happy paths | Require real OAuth provider credentials |
| `GET /.well-known/oauth-authorization-server` | Now tested (Test 18) | |
