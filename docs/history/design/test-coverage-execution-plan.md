# Frontend Test Execution Plan

> Based on test-coverage-gap-analysis.md | 179 missing tests | 38% overall coverage -> target 85%+
> Generated: 2026-06-11

---

## Execution Principles

1. **Mock-based E2E** - All tests use `page.route()` for API mocking, no real backend dependency
2. **Parallel by file** - Each spec file is independent, run in parallel via `fullyParallel: true`
3. **Helper reuse** - Extend existing `mock-api.ts` helpers, add new mock factories for error scenarios
4. **Incremental delivery** - Each phase produces a runnable, mergeable PR

---

## Phase 0: Infrastructure Preparation

**Goal**: Add reusable mock helpers for error/edge-case scenarios

**Files to modify**:
- `OAuth2Admin/tests/e2e/helpers/mock-api.ts`
- `OAuth2Frontend/tests/e2e/helpers/mock-api.ts`

**Changes**:

### Admin mock-api.ts additions

```typescript
// Add new helper factories after existing exports:

/** Mock a specific API returning an error status */
export async function mockApiError(page: Page, urlPattern: string, status: number, body: object) {
  await page.route(urlPattern, async (route) => {
    await route.fulfill({
      status,
      contentType: 'application/json',
      body: JSON.stringify(body),
    })
  })
}

/** Mock a network failure (no response) */
export async function mockNetworkError(page: Page, urlPattern: string) {
  await page.route(urlPattern, async (route) => {
    await route.abort('failed')
  })
}

/** Override a specific route that was already set by setupAuthenticatedMocks */
export async function overrideRoute(page: Page, urlPattern: string, handler: (route: any) => Promise<void>) {
  await page.route(urlPattern, handler)
}
```

### Frontend mock-api.ts additions

```typescript
// Same pattern as admin, plus:

/** Mock registration failure (duplicate user) */
export async function mockRegistrationError(page: Page, errorCode: string) {
  await page.route('**/api/register', async (route) => {
    await route.fulfill({
      status: 409,
      contentType: 'application/json',
      body: JSON.stringify({
        error: { code: errorCode, category: 'VALIDATION', message: 'User already exists' }
      }),
    })
  })
}

/** Mock password change failure */
export async function mockPasswordChangeError(page: Page) {
  await page.route('**/api/me/password', async (route) => {
    await route.fulfill({
      status: 400,
      contentType: 'application/json',
      body: JSON.stringify({
        error: { code: 'AUTH_INVALID_CREDENTIALS', message: 'Wrong password' }
      }),
    })
  })
}
```

**Verify**: Run existing test suites, confirm no regressions.
```bash
cd OAuth2Admin && npx playwright test    # ~95 tests pass
cd OAuth2Frontend && npx playwright test # ~40 tests pass
```

**Estimated time**: 1 hour

---

## Phase 1: P0 Security Tests (Both Apps)

**Goal**: Cover SQL injection, XSS, and security-critical paths that are completely untested.

### 1A: Admin Security Tests

**New file**: `OAuth2Admin/tests/e2e/security.spec.ts`

```
describe('Security', {
  // --- Injection Tests ---
  test: 'SQL injection in login username'       // A-LOGIN-008
    - Enter `' OR 1=1 --` in username, valid password
    - Verify: stays on login, error shown, no dashboard access

  test: 'XSS in login username'                 // A-LOGIN-009
    - Enter `<script>alert('xss')</script>` in username
    - Verify: error shown, no alert dialog, input rendered as text

  test: 'XSS in role name'                      // A-ROLE-012
    - Create role with `<img onerror=alert(1) src=x>`
    - Verify: role name rendered as text in table, no script execution

  test: 'XSS in scope name'                     // A-SCP-011
    - Create scope with `<script>document.cookie</script>`
    - Verify: rendered as text, no execution

  // --- Secret Exposure ---
  test: 'client secret not in DOM after modal close'  // A-SEC-004
    - Create client, view secret in modal, close modal
    - Verify: secret string not present in page DOM
    - Verify: secret not in any <input> or hidden element
})
```

**Test count**: 5 tests

### 1B: Frontend Security Tests

**New file**: `OAuth2Frontend/tests/e2e/security.spec.ts`

```
describe('Security', {
  // --- Injection Tests ---
  test: 'SQL injection in login'                // U-LOGIN-006
    - Enter `' OR 1=1 --` as username
    - Verify: error shown, stays on login

  test: 'XSS in login username'                 // U-LOGIN-007
    - Enter `<script>alert('xss')</script>`
    - Verify: no dialog, input as text

  test: 'XSS in registration username'          // U-REG-010
    - Enter `<img onerror=alert(1) src=x>` in register form
    - Verify: no dialog

  // --- Anti-enumeration ---
  test: 'forgot password anti-enumeration'      // U-FP-002
    - Submit unregistered email
    - Verify: same success message as registered email

  // --- Token exposure ---
  test: 'access token not in URL after login'   // U-SEC-003
    - Complete login flow
    - Verify: URL does not contain 'access_token' or 'token'

  test: 'localStorage cleared on account delete' // U-SEC-008
    - Delete account with matching username
    - Verify: localStorage.getItem('access_token') === null
})
```

**Test count**: 6 tests

### 1C: Admin Error Handling Tests

**New file**: `OAuth2Admin/tests/e2e/error-handling.spec.ts`

```
describe('Error Handling', {
  test: '401 response redirects to login'       // A-ERR-002
    - Login, then mock next API call as 401
    - Verify: redirect to /admin/login

  test: '500 response shows error banner'       // A-ERR-004
    - Mock dashboard stats as 500
    - Verify: error banner visible, page doesn't crash

  test: 'network failure shows error message'   // A-ERR-001
    - Abort API request
    - Verify: graceful error message

  test: '403 forbidden shows error'             // A-ERR-003
    - Mock API as 403
    - Verify: error message shown
})
```

**Test count**: 4 tests

**Verify**: Run all new tests + existing suite. All pass.

**Estimated time**: 3 hours (1A: 1h, 1B: 1h, 1C: 1h)

---

## Phase 2: P0 Frontend Critical Path Tests

**Goal**: Cover registration validation and password reset - core flows at 7-25% coverage.

### 2A: Registration Validation Tests

**New file**: `OAuth2Frontend/tests/e2e/registration-validation.spec.ts`

```
describe('Registration Validation', {
  beforeEach: setupMocks + navigate to /register

  test: 'password too short'                    // U-REG-002
    - Enter 5-char password
    - Verify: "Password must be at least 6 characters" shown

  test: 'passwords do not match'                // U-REG-003
    - Different password and confirm
    - Verify: "Passwords do not match" shown

  test: 'duplicate username'                    // U-REG-004
    - Mock register API to return 409
    - Verify: error message displayed

  test: 'duplicate email'                       // U-REG-005
    - Mock register API to return 409 with email-specific error
    - Verify: error message displayed

  test: 'empty fields validation'               // U-REG-006/007
    - Submit with empty username/email
    - Verify: HTML5 required validation

  test: 'invalid email format'                  // U-REG-008
    - Enter "not-an-email"
    - Verify: HTML5 email validation prevents submit
})
```

**Test count**: 6 tests

### 2B: Password Reset Tests

**New file**: `OAuth2Frontend/tests/e2e/password-reset.spec.ts`

```
describe('Password Reset', {
  test: 'valid reset token resets password'     // U-RP-001
    - Navigate to /reset-password?token=valid-token
    - Enter new password + confirm
    - Verify: success message, redirect to login

  test: 'expired reset token shows error'       // U-RP-002
    - Mock confirm API to return expired error
    - Verify: error message

  test: 'invalid reset token shows error'       // U-RP-003
    - Mock confirm API to return invalid token error
    - Verify: error message

  test: 'no token in URL shows error'           // U-RP-004
    - Navigate to /reset-password without token
    - Verify: error or redirect
})
```

**Test count**: 4 tests

**Estimated time**: 2.5 hours (2A: 1.5h, 2B: 1h)

---

## Phase 3: P1 Admin Functional Tests

**Goal**: Fill functional gaps in Admin Console from 54% to ~80%.

### 3A: Auth Edge Cases

**Append to**: `OAuth2Admin/tests/e2e/auth.spec.ts`

```
  test: 'empty username prevents submit'        // A-LOGIN-002
    - Clear username, click submit
    - Verify: form does not submit (check no API call via route)

  test: 'empty password prevents submit'        // A-LOGIN-003
    - Clear password, click submit
    - Verify: form does not submit

  test: 'loading state during login'            // A-LOGIN-011
    - Slow the login mock response
    - Verify: button text "Signing in...", button disabled

  test: 'non-existent user shows error'         // A-LOGIN-006
    - Mock login to return 401 for unknown user
    - Verify: error message displayed

  test: 'browser back after login redirects to dashboard'  // A-LOGIN-012
    - Login, goBack, verify still on dashboard
```

**Test count**: 5 tests

### 3B: Applications Edge Cases

**Append to**: `OAuth2Admin/tests/e2e/applications.spec.ts`

```
  test: 'no grant type selected shows error'    // A-APP-CR-003
    - Open create modal, deselect all grant types
    - Submit -> verify "Please select at least one grant type"

  test: 'multiple grant types selection'        // A-APP-CR-004
    - Select authorization_code + refresh_token + client_credentials
    - Verify: created with all types

  test: 'delete client cancel preserves client' // A-APP-005
    - Click delete, dismiss confirm dialog
    - Verify: client still in list
```

**Test count**: 3 tests

### 3C: Users Edge Cases

**Append to**: `OAuth2Admin/tests/e2e/users.spec.ts`

```
  test: 'assign multiple roles comma-separated'  // A-USR-RL-002
    - Enter "admin, user" in role input
    - Verify: PUT request body contains both roles

  test: 'empty role input prevents save'        // A-USR-RL-003
    - Click "Save Roles" with empty input
    - Verify: no API call made

  test: 'non-existent role shows error'         // A-USR-RL-005
    - Enter "superadmin" which doesn't exist
    - Mock API to return 400
    - Verify: error message
```

**Test count**: 3 tests

### 3D: User Detail Edge Cases

**Append to**: `OAuth2Admin/tests/e2e/user-detail.spec.ts`

```
  test: 'toggle email verified checkbox'        // A-USR-DT-003
    - Toggle email_verified, save
    - Verify: PUT sent with email_verified change

  test: 'save with no changes shows message'    // A-USR-DT-004
    - Click save without modifying anything
    - Verify: "No changes" message

  test: 'non-existent user ID shows error'      // A-USR-DT-011
    - Navigate to /admin/users/999999
    - Mock API to return 404
    - Verify: error message
```

**Test count**: 3 tests

### 3E: Tokens Filter Tests

**Append to**: `OAuth2Admin/tests/e2e/tokens.spec.ts`

```
  test: 'filter by client_id sends correct params'   // A-TOK-002
    - Enter client_id filter, click Apply
    - Verify: API called with client_id param
    - Verify: page reset to 1

  test: 'filter by user_id sends correct params'     // A-TOK-003
    - Enter user_id filter, click Apply
    - Verify: API called with user_id param

  test: 'clear filters resets and fetches all'       // A-TOK-004
    - Set filters, click Clear
    - Verify: API called without filters
```

**Test count**: 3 tests

### 3F: Roles Edge Cases

**Append to**: `OAuth2Admin/tests/e2e/roles.spec.ts`

```
  test: 'duplicate role name shows error'       // A-ROLE-006
    - Mock POST /api/admin/roles to return 409
    - Create role with existing name
    - Verify: error message

  test: 'delete role cancel preserves role'     // A-ROLE-009
    - Click delete, dismiss confirm
    - Verify: role still in list
```

**Test count**: 2 tests

**Estimated time**: 4 hours (3A: 1h, 3B: 0.5h, 3C: 0.5h, 3D: 0.5h, 3E: 1h, 3F: 0.5h)

---

## Phase 4: P1 Frontend Functional Tests

**Goal**: Cover MFA, security page, profile, and navigation gaps.

### 4A: MFA Validation Tests

**Append to**: `OAuth2Frontend/tests/e2e/auth.spec.ts` (MFA section)

```
  test: 'invalid MFA code shows error'          // U-MFA-003
    - Trigger MFA flow, enter wrong code
    - Mock verify to return 401
    - Verify: error message

  test: 'less than 6 digits disables verify button'  // U-MFA-004
    - Enter "1234" (4 digits)
    - Verify: button disabled

  test: 'back to login from MFA form'           // U-MFA-007
    - Click "Back to login" on MFA form
    - Verify: MFA form hidden, login form shown
```

**Test count**: 3 tests

### 4B: Security Page Tests

**Append to**: `OAuth2Frontend/tests/e2e/account.spec.ts` (Security section)

```
  test: 'password mismatch shows error'         // U-SEC-003
    - Enter different new/confirm passwords
    - Verify: "Passwords do not match"

  test: 'password too short shows error'        // U-SEC-004
    - Enter 5-char new password
    - Verify: "at least 6 characters"

  test: 'wrong old password shows error'        // U-SEC-005
    - Mock /api/me/password to return 400
    - Verify: error message from API

  test: 'MFA verify with valid code'            // U-SEC-008
    - Start MFA setup, enter valid code
    - Mock verify to return 200
    - Verify: "MFA enabled successfully!"

  test: 'MFA disable with valid password'       // U-SEC-010
    - Profile shows mfa_enabled=true
    - Enter password, click disable
    - Verify: "MFA disabled"

  test: 'MFA disable empty password shows error' // U-SEC-011
    - Click disable without entering password
    - Verify: "Password required to disable MFA"

  test: 'delete account with correct username'  // U-SEC-016
    - Enter matching username
    - Click delete
    - Verify: DELETE /api/me called, redirect to /login
```

**Test count**: 7 tests

### 4C: Profile & Authorized Apps Tests

**Append to**: `OAuth2Frontend/tests/e2e/account.spec.ts` (Profile section)

```
  test: 'unverified email shows resend button'  // U-PROF-003
    - Mock profile with email_verified=false
    - Verify: yellow "Unverified" badge + "Resend verification email" link

  test: 'resend verification email'             // U-PROF-004
    - Click resend link
    - Verify: "Verification email sent!" success

  test: 'profile API failure shows error'       // U-PROF-007
    - Mock /api/me as 500
    - Verify: "Failed to load profile" error
```

**Append to**: `OAuth2Frontend/tests/e2e/account.spec.ts` (Authorized Apps section)

```
  test: 'empty authorized apps list'            // U-APP-002
    - Mock /api/me/authorized-apps to return []
    - Verify: "No authorized applications" empty state

  test: 'revoke cancel preserves app'           // U-APP-004
    - Click Revoke, dismiss confirm
    - Verify: app still in list

  test: 'revoke failure shows error'            // U-APP-005
    - Click Revoke, confirm, mock DELETE as 500
    - Verify: error message

  test: 'app without name shows client_id'      // U-APP-006
    - Mock app with name="" but valid client_id
    - Verify: client_id shown as fallback
```

**Test count**: 7 tests

### 4D: Navigation Tests

**Append to**: `OAuth2Frontend/tests/e2e/navigation.spec.ts`

```
  test: 'user dropdown opens and closes'        // U-NAV-003
    - Click avatar, verify dropdown visible
    - Click avatar again, verify dropdown hidden

  test: 'click outside dropdown closes it'      // U-NAV-005
    - Open dropdown
    - Click outside overlay
    - Verify: dropdown closed

  test: 'active nav link highlighted'           // U-NAV-009
    - Navigate to /security
    - Verify: "Security" link has indigo styling
```

**Test count**: 3 tests

**Estimated time**: 4 hours (4A: 1h, 4B: 1.5h, 4C: 1h, 4D: 0.5h)

---

## Phase 5: P2 Polish Tests (Optional)

**Goal**: UX details, accessibility, edge cases.

### Admin P2 Tests (8 tests)

Append to existing spec files:

| Test | File | Maps To |
|------|------|---------|
| Dashboard loading placeholders ("—") | dashboard.spec.ts | A-DASH-004 |
| Failures > 0 shows red | dashboard.spec.ts | A-DASH-008 |
| Failures = 0 shows normal color | dashboard.spec.ts | A-DASH-007 |
| Success message auto-dismiss 3s | new: ux.spec.ts | A-ERR-005 |
| Error message auto-dismiss 5s | ux.spec.ts | A-ERR-006 |
| No changes save on app detail | application-detail.spec.ts | A-APP-DT-003 |
| Token timestamp formatting | tokens.spec.ts | A-TOK-012 |
| Responsive sidebar | navigation.spec.ts | A-NAV-005 |

### Frontend P2 Tests (9 tests)

| Test | File | Maps To |
|------|------|---------|
| Registration success redirect (2s) | auth.spec.ts | U-REG-013 |
| Callback loading spinner | oauth.spec.ts | U-CB-004 |
| Authorized apps empty state | account.spec.ts | U-APP-002 |
| App without name fallback | account.spec.ts | U-APP-006 |
| Success message auto-dismiss | account.spec.ts | U-APP-007 |
| Dashboard no roles "None" | account.spec.ts | U-DASH-002 |
| Dashboard multiple role badges | account.spec.ts | U-DASH-003 |
| Profile loading state | account.spec.ts | U-PROF-008 |
| WebAuthn hidden when unsupported | account.spec.ts | U-SEC-015 |

**Estimated time**: 3 hours

---

## Execution Schedule

```
Week 1: Phase 0 + Phase 1 (P0 Security)
  Day 1: Phase 0 - Infrastructure prep (1h)
  Day 1-2: Phase 1A - Admin security (1h)
  Day 2: Phase 1B - Frontend security (1h)
  Day 2: Phase 1C - Admin error handling (1h)
  Deliverable: PR with 15 new tests, 0 regressions

Week 1-2: Phase 2 (P0 Frontend Critical)
  Day 3: Phase 2A - Registration validation (1.5h)
  Day 3-4: Phase 2B - Password reset (1h)
  Deliverable: PR with 10 new tests, 0 regressions

Week 2: Phase 3 (P1 Admin Functional)
  Day 4-5: Phase 3A-3F (4h total)
  Deliverable: PR with 19 new tests, 0 regressions

Week 2-3: Phase 4 (P1 Frontend Functional)
  Day 5-7: Phase 4A-4D (4h total)
  Deliverable: PR with 20 new tests, 0 regressions

Week 3: Phase 5 (P2 Polish, Optional)
  Day 7-8: All P2 tests (3h)
  Deliverable: PR with 17 new tests, 0 regressions
```

---

## Coverage Projection

| Phase | New Tests | Admin Coverage | Frontend Coverage | Overall |
|-------|-----------|---------------|-------------------|---------|
| Start | 0 | 54% (75/140) | 24% (36/150) | 38% |
| Phase 1 | +15 | 61% | 32% | 46% |
| Phase 2 | +10 | 61% | 39% | 50% |
| Phase 3 | +19 | 78% | 39% | 58% |
| Phase 4 | +20 | 78% | 56% | 67% |
| Phase 5 | +17 | 83% | 68% | 76% |

After all phases: **81 new tests**, total ~192 tests, **76% overall coverage**.

---

## PR & Merge Strategy

Each phase is one PR:

| PR | Branch | Content | Expected CI |
|---|---|---|---|
| PR 1 | `test/phase1-security` | Phase 0 + Phase 1 (15 tests) | All existing + new pass |
| PR 2 | `test/phase2-registration` | Phase 2 (10 tests) | All pass |
| PR 3 | `test/phase3-admin-functional` | Phase 3 (19 tests) | All pass |
| PR 4 | `test/phase4-frontend-functional` | Phase 4 (20 tests) | All pass |
| PR 5 | `test/phase5-polish` | Phase 5 (17 tests) | All pass |

Each PR must:
- Pass full Playwright suite in both apps (0 regressions)
- Include only test files (no source code changes)
- Be reviewable in under 30 minutes

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| Mock drift from real API | Phase 0 mock helpers are centralized; if API changes, update one file |
| Playwright flakiness | `waitForLoadState`, explicit waits on elements; retry: 2 in CI |
| WebAuthn tests (U-SEC-013/14/15) | Marked P2; browser API mocking is complex, may skip |
| Confirm dialogs may vary by browser | Use `page.on('dialog')` listener, not browser native dialogs |
| Large PR review burden | Each phase is a separate PR, max ~20 tests per PR |
