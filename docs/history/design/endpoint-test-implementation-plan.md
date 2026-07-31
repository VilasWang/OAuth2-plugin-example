# Backend Endpoint Test Implementation Plan

> Based on endpoint-test-coverage-analysis.md | 27 untested endpoints + scenario gaps on 49 tested endpoints
> Target: 90%+ endpoint coverage, 80%+ error case coverage
> Generated: 2026-06-11

---

## Execution Principles

1. **PowerShell first** - All new endpoint tests go into the existing `test-admin-endpoints.ps1` / `test-oauth2-endpoints.ps1` scripts, maintaining the established pattern.
2. **Incremental delivery** - Each phase is a self-contained, mergeable commit.
3. **Scenario completeness** - Every tested endpoint gets at minimum: 1 happy path + 1 error case.
4. **No external dependencies** - Tests run against a running server with the default seed data (admin user, vue-client, admin-console, backend-svc clients).
5. **Idempotent cleanup** - Tests clean up their own data so the test suite is re-runnable.

---

## Phase 0: Infrastructure (Preparation)

**Goal**: Extend test infrastructure to support new test scenarios.

**Files to modify**:
- `scripts/backend/test-admin-endpoints.ps1`
- `scripts/backend/test-oauth2-endpoints.ps1`
- `scripts/backend/common-test-functions.ps1`

### Changes

```powershell
# common-test-functions.ps1 additions

# Generic assertion helper (if not already present)
function Assert-JsonField {
    param($Response, $Field, $ExpectedValue, $TestName)
    $val = $Response.$Field
    if ($val -ne $ExpectedValue) {
        throw "$TestName : expected $Field=$ExpectedValue, got $val"
    }
}

# Register a test user and return { user_id, username, access_token }
function New-TestUser {
    param($BaseUrl)
    $ts = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    $username = "eptest_${ts}"
    $body = @{
        username = $username
        password = "TestPass123!"
        email    = "${username}@ep-test.example.com"
    }
    $resp = Invoke-RestMethod -Uri "$BaseUrl/api/register" -Method Post -ContentType "application/json" -Body ($body | ConvertTo-Json)
    # Login to get token
    $loginResp = Invoke-Login -BaseUrl $BaseUrl -Username $username -Password "TestPass123!"
    return @{
        username     = $username
        access_token = $loginResp.access_token
    }
}

# Create a test OAuth2 client and return { client_id, client_secret }
function New-TestClient {
    param($BaseUrl, $AdminToken)
    # Will be implemented after Phase 1 adds POST /api/admin/clients test
}
```

**Estimated tests added**: 0 (infrastructure only)

---

## Phase 1: P0 Critical Gaps (6 endpoints, ~25 tests)

**Goal**: Cover all P0 untested endpoints.

### 1.1 Client CRUD gaps (3 endpoints)

**Target file**: `test-admin-endpoints.ps1` - Add after Section B (Client Management)

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| B7 | `GET /api/admin/clients` | List all clients | GET | 200, array with >= 2 clients |
| B8 | `POST /api/admin/clients` | Create new client | POST | 200, returns client_id + client_secret |
| B9 | `POST /api/admin/clients` | Create with duplicate name | POST | 409 |
| B10 | `POST /api/admin/clients` | Create with missing required fields | POST | 400 |
| B11 | `DELETE /api/admin/clients/:id` | Delete test client | DELETE | 200 |
| B12 | `DELETE /api/admin/clients/:id` | Delete non-existent client | DELETE | 404 |
| B13 | `POST /api/admin/clients/:id/reset-secret` | Reset client secret | POST | 200, new secret differs from old |
| B14 | `POST /api/admin/clients/:id/reset-secret` | Reset non-existent client | POST | 404 |

**Test sequence**:
```
B7 (list) -> B8 (create, store client_id) -> B9 (duplicate) -> B10 (invalid)
-> B13 (reset-secret, verify new secret) -> B11 (delete the test client) -> B12 (404) -> B14 (404)
```

### 1.2 Single token revoke (1 endpoint)

**Target file**: `test-admin-endpoints.ps1` - Add in Section C (Token Management)

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| C6 | `DELETE /api/admin/tokens/:tokenPrefix` | Revoke existing token | DELETE | 200 |
| C7 | `DELETE /api/admin/tokens/:tokenPrefix` | Revoke non-existent token | DELETE | 200 (idempotent) |

**Test sequence**: Get token prefix from C7 list, then delete it, verify userinfo returns 401.

### 1.3 Password reset confirm (1 endpoint)

**Target file**: `test-oauth2-endpoints.ps1` - Add after Test 16

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| 16b | `POST /api/password-reset/confirm` | Confirm with invalid token | POST | 400 or 401 |
| 16c | `POST /api/password-reset/confirm` | Confirm with missing fields | POST | 400 |
| 16d | `POST /api/password-reset/confirm` | Confirm with expired token | POST | 400 or 401 |

**Note**: Full happy-path confirm requires intercepting the email to get the real token. If no email mock is available, test only error paths and mark happy path as requiring manual verification or C++ integration test.

### 1.4 OAuth2 consent (1 endpoint)

**Target file**: `test-oauth2-endpoints.ps1` - Add after Test 5

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| 5b | `POST /oauth2/consent` | Consent without session | POST | 401 |
| 5c | `POST /oauth2/consent` | Consent with invalid scope | POST | 400 |

**Note**: Full consent flow requires a valid session from authorize endpoint. The happy path is covered indirectly by the existing authorization code flow tests (login -> token works implies consent succeeded). These tests add explicit consent endpoint coverage.

### 1.5 Dynamic client registration (1 endpoint)

**Target file**: `test-oauth2-endpoints.ps1` - Add new section

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| 18 | `POST /oauth2/register` | Register new client (RFC 7591) | POST | 200, returns client_id + client_secret |
| 19 | `POST /oauth2/register` | Register with missing client_name | POST | 400 |
| 20 | `POST /oauth2/register` | Register with invalid redirect_uris | POST | 400 |

**Phase 1 totals**: ~25 new tests across 6 endpoints

---

## Phase 2: P1 MFA & User Self-Service (11 endpoints, ~30 tests)

**Goal**: Cover MFA flow and user self-service endpoints.

### 2.1 MFA full flow (4 endpoints)

**Target file**: `test-oauth2-endpoints.ps1` - Add new MFA section

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| MFA1 | `POST /api/me/mfa/setup` | Setup MFA for logged-in user | POST | 200, returns secret + QR URL |
| MFA2 | `POST /api/me/mfa/setup` | Setup without auth | POST | 401 |
| MFA3 | `POST /api/me/mfa/verify` | Verify with valid TOTP code | POST | 200 |
| MFA4 | `POST /api/me/mfa/verify` | Verify with invalid code | POST | 400 |
| MFA5 | `POST /api/me/mfa/disable` | Disable MFA | POST | 200 |
| MFA6 | `POST /api/me/mfa/disable` | Disable without auth | POST | 401 |
| MFA7 | `POST /oauth2/mfa/verify` | Verify TOTP during login (no session) | POST | 401 |
| MFA8 | `POST /oauth2/mfa/verify` | Verify with invalid code format | POST | 400 |

**Test sequence**: Register test user -> login -> MFA1 (setup) -> MFA3 (verify with computed TOTP) -> MFA5 (disable) -> cleanup.

**Implementation note**: TOTP code computation requires the shared secret from setup response. Use a TOTP library or compute HMAC-SHA1 in PowerShell:

```powershell
# Simplified TOTP computation
function Get-TotpCode {
    param([string]$Secret)
    # Base32 decode secret, compute TOTP with 30s step
    # Return 6-digit code
}
```

### 2.2 User self-service (3 endpoints)

**Target file**: `test-oauth2-endpoints.ps1` - Extend user section

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| US1 | `GET /api/me/authorized-apps` | List authorized apps | GET | 200, array |
| US2 | `GET /api/me/authorized-apps` | List without auth | GET | 401 |
| US3 | `DELETE /api/me/authorized-apps/:clientId` | Revoke app authorization | DELETE | 200 |
| US4 | `DELETE /api/me/authorized-apps/:clientId` | Revoke non-existent app | DELETE | 404 |
| US5 | `DELETE /api/me` | Delete own account | DELETE | 200 |
| US6 | `DELETE /api/me` | Delete without auth | DELETE | 401 |

**Note**: US5 (account deletion) must run last and use a dedicated test user.

### 2.3 Email verification (2 endpoints)

**Target file**: `test-oauth2-endpoints.ps1` - Add new section

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| EV1 | `GET /api/verify-email` | Verify with invalid token | GET | 400 |
| EV2 | `GET /api/verify-email` | Verify with missing token | GET | 400 |
| EV3 | `POST /api/verify-email/resend` | Resend without auth | POST | 401 |
| EV4 | `POST /api/verify-email/resend` | Resend with auth | POST | 200 |

### 2.4 Client list (1 endpoint)

**Target file**: `test-admin-endpoints.ps1` - Section B (already planned in Phase 1 as B7)

### 2.5 Enhanced password change scenarios

**Target file**: `test-oauth2-endpoints.ps1` - Extend Test 17

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| 17b | `PUT /api/me/password` | Change with wrong old password | PUT | 401 or 400 |
| 17c | `PUT /api/me/password` | Change with weak new password | PUT | 400 |
| 17d | `PUT /api/me/password` | Change without auth | PUT | 401 |

**Phase 2 totals**: ~30 new tests across 11 endpoints

---

## Phase 3: P2 WebAuthn, Social Login, Device Flow (12 endpoints, ~20 tests)

**Goal**: Cover remaining feature endpoints.

### 3.1 WebAuthn flow (5 endpoints)

**Target file**: `test-oauth2-endpoints.ps1` - Add new WebAuthn section

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| WA1 | `POST /api/me/webauthn/register/begin` | Begin registration | POST | 200, returns challenge |
| WA2 | `POST /api/me/webauthn/register/begin` | Begin without auth | POST | 401 |
| WA3 | `POST /api/me/webauthn/register/finish` | Finish with invalid attestation | POST | 400 |
| WA4 | `POST /oauth2/webauthn/authenticate/begin` | Begin authentication | POST | 200, returns challenge |
| WA5 | `POST /oauth2/webauthn/authenticate/finish` | Finish with invalid assertion | POST | 400 |
| WA6 | `GET /api/me/webauthn/credentials` | List credentials | GET | 200, array |
| WA7 | `GET /api/me/webauthn/credentials` | List without auth | GET | 401 |

**Note**: Full WebAuthn happy path requires a real authenticator or mock. Phase 3 tests auth gating and error handling only. Full flow could be tested in C++ integration tests with a software authenticator mock.

### 3.2 Device authorization flow (2 endpoints)

**Target file**: `test-oauth2-endpoints.ps1` - Add new Device section

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| DA1 | `POST /oauth2/device_authorization` | Request device code | POST | 200, returns device_code + user_code + verification_uri |
| DA2 | `POST /oauth2/device_authorization` | Missing client_id | POST | 400 |
| DA3 | `POST /oauth2/device/approve` | Approve without auth | POST | 401 |
| DA4 | `POST /oauth2/device/approve` | Approve with invalid device_code | POST | 400 |

### 3.3 Social login (3 endpoints)

**Target file**: `test-oauth2-endpoints.ps1` - Add new Social section

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| SL1 | `POST /api/github/login` | Login with invalid code | POST | 401 |
| SL2 | `POST /api/github/login` | Login with missing code | POST | 400 |
| SL3 | `POST /api/google/login` | Login with invalid code | POST | 401 |
| SL4 | `POST /api/google/login` | Login with missing code | POST | 400 |
| SL5 | `POST /api/wechat/login` | Login with invalid code | POST | 401 |
| SL6 | `POST /api/wechat/login` | Login with missing code | POST | 400 |

**Note**: Happy-path social login requires real OAuth provider credentials. Only error cases are testable without mocks.

### 3.4 RFC 8414 metadata (1 endpoint)

| Test # | Endpoint | Scenario | Method | Expected |
|--------|----------|----------|--------|----------|
| META1 | `GET /.well-known/oauth-authorization-server` | Get server metadata | GET | 200, contains issuer + token_endpoint |

**Phase 3 totals**: ~20 new tests across 12 endpoints

---

## Phase 4: Scenario Hardening (existing endpoints, ~20 tests)

**Goal**: Fill scenario gaps on already-tested endpoints.

### 4.1 OAuth2 token endpoint variants

| Test # | Scenario | Expected |
|--------|----------|----------|
| TV1 | `password` grant type (if enabled) | 200 or `unsupported_grant_type` |
| TV2 | Expired authorization code | 400, `invalid_grant` |
| TV3 | Already-used authorization code | 400, `invalid_grant` |
| TV4 | Token with no scopes requested | 200, default scopes |

### 4.2 Token introspection edge cases

| Test # | Scenario | Expected |
|--------|----------|----------|
| TI1 | Introspect malformed token | 200, `active: false` |
| TI2 | Introspect expired token | 200, `active: false` |
| TI3 | Introspect with wrong client credentials | 401 |

### 4.3 Admin endpoint edge cases

| Test # | Scenario | Expected |
|--------|----------|----------|
| AE1 | `GET /api/admin/tokens?per_page=10000` | 200 or 400 (limit validation) |
| AE2 | `GET /api/admin/tokens?page=-1` | 200 or 400 |
| AE3 | `PUT /api/admin/clients/:id` with empty body | 200 or 400 |
| AE4 | `POST /api/admin/roles` with empty name | 400 |
| AE5 | `PUT /api/admin/scopes/:id` with invalid mapped_role | 400 |

### 4.4 Authorization & security edge cases

| Test # | Scenario | Expected |
|--------|----------|----------|
| SE1 | `POST /oauth2/revoke` already-revoked token | 200 (idempotent) |
| SE2 | `POST /oauth2/introspect` with hint_type | 200, `active: true/false` |
| SE3 | `GET /api/admin/dashboard/stats` non-admin token | 403 |
| SE4 | `POST /oauth2/login` with locked account | 403 or 401 |

**Phase 4 totals**: ~20 new tests strengthening existing coverage

---

## Phase 5: Bash Script Parity

**Goal**: Sync `.sh` scripts with all new PowerShell tests.

**Files to modify**:
- `scripts/backend/test-admin-endpoints.sh`
- `scripts/backend/test-oauth2-endpoints.sh`

Every test added in Phases 1-4 to the PowerShell scripts must have an equivalent test in the Bash scripts. This is a mechanical translation, not a design phase.

---

## Implementation Timeline

| Phase | Endpoints | Tests | Estimated Effort | Depends On |
|-------|-----------|-------|-----------------|------------|
| Phase 0 | 0 | 0 | 1-2 hours | - |
| Phase 1 | 6 | ~25 | 4-6 hours | Phase 0 |
| Phase 2 | 11 | ~30 | 6-8 hours | Phase 0 |
| Phase 3 | 12 | ~20 | 4-6 hours | Phase 0 |
| Phase 4 | 0 (hardening) | ~20 | 3-4 hours | Phases 1-3 |
| Phase 5 | 0 (parity) | 0 (mirror) | 3-4 hours | Phases 1-4 |
| **Total** | **29** | **~95** | **~21-30 hours** | - |

---

## Expected Coverage After Completion

| Dimension | Current | After Plan | Target |
|-----------|---------|------------|--------|
| Endpoint coverage | 64% (49/76) | ~95% (72/76) | 90%+ |
| Happy path coverage | ~70% | ~90% | 95%+ |
| Error case coverage | ~40% | ~75% | 80%+ |
| Edge case coverage | ~30% | ~55% | 60%+ |

### Remaining untested (by design)

These endpoints are intentionally excluded:

| Endpoint | Reason |
|----------|--------|
| `GET /login` | Returns HTML page, not JSON API; covered by frontend E2E |
| `GET /docs/api/openapi.json` | Static file serving |
| `GET /docs/api` + `GET /docs/api/` | Swagger UI static files |
| Social login happy paths | Require real OAuth provider credentials |

---

## Test Naming Convention

New tests follow the existing pattern in each script:

```
Test {N}: {Brief Description}
  {method} {url}
  Expect: {status}
  {assertion details}
```

For the admin script, tests continue numbering from 38+. For the OAuth2 script, tests continue from the highest existing test number.

---

## Verification Checklist

After each phase:

- [ ] All new tests pass against a running server with default seed data
- [ ] No test leaves orphan data (all created resources are cleaned up)
- [ ] Full test suite (admin + oauth2) runs without failures
- [ ] Bash script has matching tests (Phase 5, or noted as TODO)
- [ ] `docs/backend/endpoint-test-coverage-analysis.md` matrix updated
