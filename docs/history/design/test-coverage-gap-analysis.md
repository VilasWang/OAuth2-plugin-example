# Frontend Test Coverage Gap Analysis

> Comparison of test-cases.md (design specs) vs existing Playwright E2E tests.
> Generated: 2026-06-11

---

## 1. OAuth2Admin - Coverage Gap Analysis

### Existing Test Count: ~95 E2E tests across 13 spec files

### 1.1 Authentication (`auth.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| A-LOGIN-001 | Valid admin credentials | **Covered** | `successful login navigates to dashboard` |
| A-LOGIN-002 | Empty username | **Missing** | HTML5 validation not tested |
| A-LOGIN-003 | Empty password | **Missing** | HTML5 validation not tested |
| A-LOGIN-004 | Both fields empty | **Missing** | |
| A-LOGIN-005 | Wrong password | **Covered** | `shows error on login failure` |
| A-LOGIN-006 | Non-existent user | **Missing** | |
| A-LOGIN-007 | Non-admin user | **Covered** | `denies access for non-admin users` |
| A-LOGIN-008 | SQL injection in username | **Missing** | Security-critical |
| A-LOGIN-009 | XSS in username | **Missing** | Security-critical |
| A-LOGIN-010 | Whitespace-only username | **Missing** | |
| A-LOGIN-011 | Loading state | **Missing** | Button disabled/"Signing in..." text |
| A-LOGIN-012 | Browser back after login | **Missing** | |
| A-LOGIN-013 | Direct access to protected page | **Covered** | `redirects to login when not authenticated` |
| A-LOGIN-014 | Session persistence | **Missing** | |
| A-LOGIN-015 | Concurrent login attempts (rapid clicks) | **Missing** | Button debounce |
| A-LOGOUT-001 | Normal logout | **Covered** | `logout clears session and redirects to login` |
| A-LOGOUT-002 | Access after logout | **Covered** | `protected routes redirect after sign out` |
| A-LOGOUT-003 | Browser back after logout | **Missing** | |
| A-LOGIN-MFA | MFA handling | **Covered** | `handles MFA required response` |

**Gap Count: 10 missing tests**

### 1.2 Dashboard (`dashboard.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| A-DASH-001 | Stats display | **Covered** | `displays stats cards with real data` |
| A-DASH-002 | System health | **Covered** | `displays system health status` |
| A-DASH-003 | Quick action links | **Covered** | `quick action links navigate correctly` |
| A-DASH-004 | Loading state | **Missing** | |
| A-DASH-005 | API failure | **Covered** | `shows unhealthy status when backend is down` |
| A-DASH-006 | Stats API failure only | **Missing** | Health OK but stats fail |
| A-DASH-007 | Failures today = 0 color | **Missing** | |
| A-DASH-008 | Failures today > 0 color | **Missing** | Red color conditional |

**Gap Count: 4 missing tests**

### 1.3 Applications (`applications.spec.ts` + `application-detail.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| A-APP-001 | List loads | **Covered** | |
| A-APP-002 | Empty list | **Covered** | `shows empty state when no clients exist` |
| A-APP-003 | Navigate to detail | **Covered** (implicitly) | via list interaction |
| A-APP-004 | Delete client | **Covered** | |
| A-APP-005 | Delete cancel | **Missing** | Cancel confirm dialog |
| A-APP-006 | Reset secret from list | **Covered** | |
| A-APP-CR-001 | Create CONFIDENTIAL client | **Covered** | |
| A-APP-CR-002 | Empty name | **Missing** | |
| A-APP-CR-003 | No grant type selected | **Missing** | |
| A-APP-CR-004 | Multiple grant types | **Missing** | |
| A-APP-CR-005 | Duplicate client name | **Missing** | |
| A-APP-CR-006 | Very long redirect URI | **Missing** | |
| A-APP-CR-007 | Invalid redirect URI format | **Missing** | |
| A-APP-CR-008 | Device code grant type | **Missing** | |
| A-APP-CR-009 | Close modal without submit | **Covered** | `cancel button closes create modal` |
| A-APP-CR-010 | Loading state | **Missing** | |
| A-APP-DT-001 | Info tab | **Covered** | |
| A-APP-DT-002 | Save name change | **Covered** | `Save Changes works on Info tab` |
| A-APP-DT-003 | No changes save | **Missing** | |
| A-APP-DT-004 | Edit redirect URIs | **Covered** | `Auth Config tab shows redirect URIs` |
| A-APP-DT-005 | Invalid application ID | **Missing** | |
| A-APP-DT-006 | Scopes tab | **Covered** | |
| A-APP-DT-007 | Save scopes | **Covered** | |
| A-APP-DT-008 | Reset secret | **Covered** | |
| A-APP-DT-009 | Copy to clipboard | **Covered** | `Info tab has copy button` |
| A-APP-DT-010 | Credentials tab | **Covered** | |

**Gap Count: 10 missing tests**

### 1.4 Users (`users.spec.ts` + `user-detail.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| A-USR-001 | List loads | **Covered** | |
| A-USR-002 | Verified badge | **Covered** | |
| A-USR-003 | Unverified badge | **Covered** | |
| A-USR-004 | MFA enabled | **Covered** | |
| A-USR-005 | MFA disabled | **Covered** | |
| A-USR-006 | Navigate to detail | **Missing** | "Details" link click |
| A-USR-007 | API error | **Missing** | |
| A-USR-RL-001 | Assign single role | **Covered** | |
| A-USR-RL-002 | Assign multiple roles | **Missing** | |
| A-USR-RL-003 | Empty role input | **Missing** | |
| A-USR-RL-004 | Whitespace roles | **Missing** | |
| A-USR-RL-005 | Non-existent role | **Missing** | |
| A-USR-RL-006 | Cancel role assignment | **Covered** | |
| A-USR-RL-007 | Loading state | **Missing** | |
| A-USR-DT-001 | Info tab | **Covered** | |
| A-USR-DT-002 | Edit email | **Covered** | `can save info changes` |
| A-USR-DT-003 | Toggle email verified | **Missing** | |
| A-USR-DT-004 | No changes save | **Missing** | |
| A-USR-DT-005 | Roles tab | **Covered** | |
| A-USR-DT-006 | Save roles | **Covered** | |
| A-USR-DT-007 | Disable user | **Covered** | `disable account button is visible` |
| A-USR-DT-008 | Enable user | **Covered** | `shows Enable Account button for locked user` |
| A-USR-DT-009 | Security tab | **Covered** | |
| A-USR-DT-010 | Locked user | **Covered** | |
| A-USR-DT-011 | Non-existent user | **Missing** | |
| A-USR-DT-012 | Concurrent role edit | **Missing** | Race condition |

**Gap Count: 10 missing tests**

### 1.5 Roles (`roles.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| A-ROLE-001 | List roles | **Covered** | |
| A-ROLE-002 | Built-in badge | **Covered** | |
| A-ROLE-003 | Built-in cannot be deleted | **Covered** | |
| A-ROLE-004 | Create role | **Covered** | |
| A-ROLE-005 | Empty name | **Covered** | `Create button disabled when name is empty` |
| A-ROLE-006 | Duplicate role name | **Missing** | |
| A-ROLE-007 | Edit description | **Covered** | |
| A-ROLE-008 | Delete custom role | **Covered** | |
| A-ROLE-009 | Delete cancel | **Missing** | Cancel confirm dialog |
| A-ROLE-010 | Role with assigned users | **Missing** | Cascading effect |
| A-ROLE-011 | Empty role list | **Missing** | |
| A-ROLE-012 | XSS in role name | **Missing** | |
| A-ROLE-013 | Very long role name | **Missing** | |

**Gap Count: 6 missing tests**

### 1.6 Scopes (`scopes-management.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| A-SCP-001 | List scopes | **Covered** | |
| A-SCP-002 | Built-in indicator | **Covered** | |
| A-SCP-003 | Create scope | **Covered** | |
| A-SCP-004 | Empty name | **Covered** | |
| A-SCP-005 | Duplicate scope | **Missing** | |
| A-SCP-006 | Edit scope | **Covered** | |
| A-SCP-007 | Toggle is_default | **Covered** | `edit modal shows checkboxes` |
| A-SCP-008 | Toggle requires_admin_role | **Covered** | |
| A-SCP-009 | Delete scope | **Covered** | |
| A-SCP-010 | Delete built-in scope | **Covered** | `Delete button not shown for built-in scopes` |
| A-SCP-011 | XSS in scope name | **Missing** | |

**Gap Count: 2 missing tests**

### 1.7 Tokens (`tokens.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| A-TOK-001 | List tokens | **Covered** | |
| A-TOK-002 | Filter by client_id | **Missing** | Filter interaction not tested |
| A-TOK-003 | Filter by user_id | **Missing** | |
| A-TOK-004 | Clear filters | **Missing** | |
| A-TOK-005 | Revoke single token | **Covered** | |
| A-TOK-006 | Revoke by client | **Covered** | `shows Revoke All by App dropdown` |
| A-TOK-007 | Revoke by user | **Covered** | `Revoke All for User button appears when user filter is set` |
| A-TOK-008 | Revoke by user without filter | **Missing** | Guard behavior |
| A-TOK-009 | Pagination | **Covered** | `shows pagination info` |
| A-TOK-010 | Empty token list | **Covered** | |
| A-TOK-011 | Confirm cancel | **Covered** | `cancel confirmation closes dialog` |
| A-TOK-012 | Timestamp formatting | **Missing** | |

**Gap Count: 5 missing tests**

### 1.8 Audit Logs (`logs.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| A-LOG-001 | List loads | **Covered** | |
| A-LOG-002 | Empty logs | **Covered** | |
| A-LOG-003 | Pagination | **Covered** | |
| A-LOG-004 | Filter by action type | **Missing** | |
| A-LOG-005 | Timestamp ordering | **Missing** | |

**Gap Count: 2 missing tests**

### 1.9 Settings (`settings.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| A-SET-001 | Page loads | **Covered** | |
| A-SET-002 | Save settings | **Missing** | |
| A-SET-003 | Invalid setting value | **Missing** | |
| A-SET-004 | No changes save | **Missing** | |

**Gap Count: 3 missing tests**

### 1.10 Navigation (`navigation.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| A-NAV-001 | Sidebar navigation | **Covered** | |
| A-NAV-002 | Active state on detail pages | **Covered** | |
| A-NAV-003 | Top bar title | **Missing** | |
| A-NAV-004 | User info in sidebar | **Covered** | |
| A-NAV-005 | Responsive layout | **Missing** | |

**Gap Count: 2 missing tests**

### 1.11 Cross-Cutting Concerns

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| A-ERR-001 | Network error | **Missing** | |
| A-ERR-002 | 401 Unauthorized | **Missing** | Session expiry redirect |
| A-ERR-003 | 403 Forbidden | **Missing** | |
| A-ERR-004 | 500 Server error | **Missing** | |
| A-ERR-005 | Success message auto-dismiss | **Missing** | |
| A-ERR-006 | Error message auto-dismiss | **Missing** | |
| A-SEC-001 | CSRF protection | **Missing** | |
| A-SEC-002 | Token storage security | **Missing** | |
| A-SEC-003 | Route guard bypass | **Covered** | Auth redirect tested |
| A-SEC-004 | Client secret display | **Covered** | Secret modal tested |
| A-PERF-001 | Large user list | **Missing** | |
| A-PERF-002 | Dashboard concurrent requests | **Missing** | |
| A-PERF-003 | Lazy-loaded routes | **Missing** | |

**Gap Count: 11 missing tests**

### Admin Summary

| Module | Total | Covered | Missing | Coverage |
|--------|-------|---------|---------|----------|
| Authentication | 19 | 9 | 10 | 47% |
| Dashboard | 8 | 4 | 4 | 50% |
| Applications | 25 | 15 | 10 | 60% |
| Users | 25 | 15 | 10 | 60% |
| Roles | 13 | 7 | 6 | 54% |
| Scopes | 11 | 9 | 2 | 82% |
| Tokens | 12 | 7 | 5 | 58% |
| Audit Logs | 5 | 3 | 2 | 60% |
| Settings | 4 | 1 | 3 | 25% |
| Navigation | 5 | 3 | 2 | 60% |
| Cross-Cutting | 13 | 2 | 11 | 15% |
| **Total** | **140** | **75** | **65** | **54%** |

---

## 2. OAuth2Frontend - Coverage Gap Analysis

### Existing Test Count: ~40 E2E tests + 3 property-based unit test files

### 2.1 Login (`auth.spec.ts` - Login section)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| U-LOGIN-001 | Valid credentials | **Covered** | `successful login redirects to dashboard` |
| U-LOGIN-002 | Empty username | **Missing** | |
| U-LOGIN-003 | Empty password | **Missing** | |
| U-LOGIN-004 | Wrong password | **Covered** | `shows error on invalid credentials` |
| U-LOGIN-005 | Non-existent user | **Missing** | |
| U-LOGIN-006 | SQL injection | **Missing** | |
| U-LOGIN-007 | XSS in username | **Missing** | |
| U-LOGIN-008 | Loading state | **Missing** | |
| U-LOGIN-009 | Redirect after login | **Missing** | `?redirect=` query param |
| U-LOGIN-010 | Already authenticated | **Covered** | `authenticated user redirected from login` |
| U-LOGIN-011 | GitHub social login | **Covered** | |
| U-LOGIN-012 | GitHub client_id not configured | **Missing** | |
| U-LOGIN-013 | Link to register | **Covered** | |
| U-LOGIN-014 | Link to forgot password | **Covered** | |
| U-LOGIN-015 | Browser autofill | **Missing** | Low priority |

**Gap Count: 9 missing tests**

### 2.2 MFA Challenge (`auth.spec.ts` - MFA section)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| U-MFA-001 | MFA required flow | **Covered** | `shows MFA challenge when required` |
| U-MFA-002 | Valid MFA code | **Covered** | `MFA verification completes login` |
| U-MFA-003 | Invalid MFA code | **Missing** | |
| U-MFA-004 | Less than 6 digits | **Missing** | Button disabled |
| U-MFA-005 | More than 6 digits | **Missing** | maxlength=6 |
| U-MFA-006 | Non-numeric input | **Missing** | |
| U-MFA-007 | Back to login | **Missing** | |
| U-MFA-008 | Loading state | **Missing** | |
| U-MFA-009 | Expired MFA token | **Missing** | |

**Gap Count: 7 missing tests**

### 2.3 Registration (`auth.spec.ts` - Register section)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| U-REG-001 | Valid registration | **Covered** | `successful registration shows success message` |
| U-REG-002 | Password too short | **Missing** | |
| U-REG-003 | Passwords don't match | **Missing** | |
| U-REG-004 | Duplicate username | **Missing** | |
| U-REG-005 | Duplicate email | **Missing** | |
| U-REG-006 | Empty username | **Missing** | |
| U-REG-007 | Empty email | **Missing** | |
| U-REG-008 | Invalid email format | **Missing** | |
| U-REG-009 | SQL injection | **Missing** | |
| U-REG-010 | XSS in username | **Missing** | |
| U-REG-011 | Very long username | **Missing** | |
| U-REG-012 | Loading state | **Missing** | |
| U-REG-013 | Success redirect timing | **Missing** | 2s timer |
| U-REG-014 | Link to login | **Missing** | |
| U-REG-015 | Already authenticated | **Missing** | |

**Gap Count: 14 missing tests**

### 2.4 Forgot Password (`auth.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| U-FP-001 | Valid email | **Covered** | |
| U-FP-002 | Unregistered email | **Missing** | Anti-enumeration |
| U-FP-003 | Invalid email format | **Missing** | |
| U-FP-004 | Empty email | **Missing** | |
| U-FP-005 | Loading state | **Missing** | |
| U-FP-006 | Back to login link | **Missing** | |
| U-FP-007 | API error handling | **Missing** | Anti-enumeration by design |

**Gap Count: 6 missing tests**

### 2.5 Reset Password & Email Verification

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| U-RP-001 to U-RP-004 | Reset Password | **Missing** | Entire page untested |
| U-VE-001 to U-VE-004 | Email Verification | **Partial** | Only valid/missing token tested |

**Gap Count: 6 missing tests**

### 2.6 OAuth2 Flows (`oauth.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| U-CB-001 | Valid callback | **Covered** | |
| U-CB-002 | Error from provider | **Covered** | |
| U-CB-003 | No code parameter | **Covered** | |
| U-CB-004 | Loading spinner | **Missing** | |
| U-CB-005 | Invalid auth code | **Missing** | |
| U-CB-006 | Expired auth code | **Missing** | |
| U-CB-007 | Back to login link | **Missing** | |
| U-GH-001 to U-GH-003 | GitHub Callback | **Missing** | Entire page untested |
| U-CON-001 to U-CON-004 | Consent Page | **Covered** | 3 tests |
| U-DV-001 to U-DV-004 | Device Verify | **Covered** | 3 tests (missing invalid/expired code) |

**Gap Count: 8 missing tests**

### 2.7 Dashboard & Account (`account.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| U-DASH-001 | Dashboard loads | **Covered** | |
| U-DASH-002 | No roles | **Missing** | |
| U-DASH-003 | Multiple roles | **Missing** | |
| U-DASH-004 | Quick links | **Covered** | |
| U-DASH-005 | Unauthenticated access | **Covered** | via navigation.spec.ts |
| U-DASH-006 | Session restore | **Missing** | |
| U-DASH-007 | Session restore failure | **Missing** | |
| U-PROF-001 | Profile loads | **Covered** | |
| U-PROF-002 | Email verified | **Covered** | |
| U-PROF-003 | Email unverified + resend | **Missing** | |
| U-PROF-004 | Resend verification | **Missing** | |
| U-PROF-005 | Resend failure | **Missing** | |
| U-PROF-006 | No email | **Missing** | |
| U-PROF-007 | API failure | **Missing** | |
| U-PROF-008 | Loading state | **Missing** | |

**Gap Count: 12 missing tests**

### 2.8 Security Page (`account.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| U-SEC-001 | Page loads | **Covered** | |
| U-SEC-002 | Change password valid | **Covered** | |
| U-SEC-003 | Password mismatch | **Missing** | |
| U-SEC-004 | Password too short | **Missing** | |
| U-SEC-005 | Wrong old password | **Missing** | |
| U-SEC-006 | Empty fields | **Missing** | |
| U-SEC-007 | MFA setup | **Covered** | |
| U-SEC-008 | MFA verify valid | **Missing** | |
| U-SEC-009 | MFA verify invalid | **Missing** | |
| U-SEC-010 | MFA disable valid | **Missing** | |
| U-SEC-011 | MFA disable empty password | **Missing** | |
| U-SEC-012 | MFA disable wrong password | **Missing** | |
| U-SEC-013 | WebAuthn register | **Missing** | Hard to test (browser API) |
| U-SEC-014 | WebAuthn cancel | **Missing** | |
| U-SEC-015 | WebAuthn not supported | **Missing** | |
| U-SEC-016 | Delete account correct username | **Missing** | |
| U-SEC-017 | Delete account wrong username | **Covered** | `delete account button is disabled until username matches` |
| U-SEC-018 | Delete account empty | **Missing** | |
| U-SEC-019 | Loading states | **Missing** | |

**Gap Count: 15 missing tests**

### 2.9 Authorized Apps (`account.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| U-APP-001 | List apps | **Covered** | |
| U-APP-002 | Empty list | **Missing** | |
| U-APP-003 | Revoke app | **Covered** | |
| U-APP-004 | Revoke cancel | **Missing** | |
| U-APP-005 | Revoke failure | **Missing** | |
| U-APP-006 | App without name | **Missing** | |
| U-APP-007 | Success auto-dismiss | **Missing** | |
| U-APP-008 | Loading state | **Missing** | |

**Gap Count: 6 missing tests**

### 2.10 Navigation (`navigation.spec.ts`)

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| U-NAV-001 | Top navigation links | **Covered** | |
| U-NAV-002 | Logo link | **Missing** | |
| U-NAV-003 | User dropdown | **Missing** | |
| U-NAV-004 | Dropdown navigation | **Missing** | |
| U-NAV-005 | Click outside dropdown | **Missing** | |
| U-NAV-006 | Logout from dropdown | **Covered** | `sign out clears session` |
| U-NAV-007 | Sticky header | **Missing** | |
| U-NAV-008 | Responsive nav | **Missing** | |
| U-NAV-009 | Active nav state | **Missing** | |

**Gap Count: 7 missing tests**

### 2.11 Cross-Cutting Concerns

| Test Case ID | Description | Status | Notes |
|---|---|---|---|
| U-ERR-001 | Network error | **Missing** | |
| U-ERR-002 | 401 Unauthorized | **Missing** | |
| U-ERR-003 | 500 Server error | **Missing** | |
| U-ERR-004 | Success auto-dismiss | **Missing** | |
| U-ERR-005 | Error normalization | **Covered** | Property-based tests exist |
| U-SEC-001 | Route guard protected | **Covered** | |
| U-SEC-002 | Route guard guest | **Covered** | |
| U-SEC-003 | Token not in URL | **Missing** | |
| U-SEC-004 | Password masking | **Missing** | |
| U-SEC-005 | Anti-enumeration | **Missing** | |
| U-SEC-006 | CSRF | **Missing** | |
| U-SEC-007 | localStorage cleared on logout | **Covered** | `sign out clears session` |
| U-SEC-008 | localStorage cleared on delete | **Missing** | |
| U-SESS-001 | Session restore on reload | **Missing** | |
| U-SESS-002 | Session expire | **Missing** | |
| U-SESS-003 | Multiple tabs | **Missing** | |
| U-SESS-004 | Token refresh | **Missing** | |
| U-A11Y-001 to U-A11Y-006 | Accessibility | **Missing** | Entire category untested |
| U-EDGE-001 to U-EDGE-010 | Edge cases | **Missing** | Entire category untested |

**Gap Count: ~22 missing tests**

### Frontend Summary

| Module | Total | Covered | Missing | Coverage |
|--------|-------|---------|---------|----------|
| Login | 15 | 6 | 9 | 40% |
| MFA Challenge | 9 | 2 | 7 | 22% |
| Registration | 15 | 1 | 14 | 7% |
| Forgot Password | 7 | 1 | 6 | 14% |
| Reset/Verify | 8 | 2 | 6 | 25% |
| OAuth2 Flows | 17 | 9 | 8 | 53% |
| Dashboard & Profile | 15 | 4 | 11 | 27% |
| Security | 19 | 3 | 16 | 16% |
| Authorized Apps | 8 | 2 | 6 | 25% |
| Navigation | 9 | 2 | 7 | 22% |
| Cross-Cutting | ~28 | 4 | ~24 | 14% |
| **Total** | **~150** | **~36** | **~114** | **24%** |

---

## 3. Recommended Priority Test Additions

### 3.1 P0 - Must Add (Security & Critical Path)

These tests cover security vulnerabilities or critical user flows that are completely untested:

#### Admin Console

```
New File: OAuth2Admin/tests/e2e/security.spec.ts
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| SQL injection in username | Enter `' OR 1=1 --`, verify error/no data leak | A-LOGIN-008 |
| XSS in login username | Enter `<script>alert(1)</script>`, verify no execution | A-LOGIN-009 |
| XSS in role name | Create role with `<img onerror>`, verify text rendering | A-ROLE-012 |
| XSS in scope name | Create scope with malicious input | A-SCP-011 |
| Client secret not persisted | After creating client, verify secret not in DOM after modal close | A-SEC-004 |

```
New File: OAuth2Admin/tests/e2e/error-handling.spec.ts
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| 401 triggers redirect | Mock API to return 401, verify redirect to login | A-ERR-002 |
| 500 shows error banner | Mock API to return 500, verify error message | A-ERR-004 |
| Network error handling | Abort API request, verify graceful message | A-ERR-001 |

#### User Frontend

```
New File: OAuth2Frontend/tests/e2e/security.spec.ts
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| SQL injection in login | Enter `' OR 1=1 --`, verify error | U-LOGIN-006 |
| XSS in login username | Enter `<script>`, verify no execution | U-LOGIN-007 |
| XSS in registration username | Enter malicious input in register form | U-REG-010 |
| Anti-enumeration forgot password | Submit unregistered email, verify same success message | U-FP-002 |
| Token not visible in URL | After login, verify no access_token in URL | U-SEC-003 |
| localStorage cleared on account delete | Delete account, verify storage cleared | U-SEC-008 |

```
New File: OAuth2Frontend/tests/e2e/registration-validation.spec.ts
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| Password too short (< 6 chars) | Verify "Password must be at least 6 characters" | U-REG-002 |
| Passwords don't match | Verify "Passwords do not match" | U-REG-003 |
| Duplicate username | Verify API error shown | U-REG-004 |
| Invalid email format | HTML5 validation | U-REG-008 |

```
New File: OAuth2Frontend/tests/e2e/password-reset.spec.ts
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| Valid reset token | Navigate with valid token, reset password | U-RP-001 |
| Expired reset token | Navigate with expired token, verify error | U-RP-002 |
| Invalid reset token | Navigate with random token, verify error | U-RP-003 |

### 3.2 P1 - Should Add (Functional Completeness)

#### Admin Console

```
Append to: OAuth2Admin/tests/e2e/auth.spec.ts
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| Empty username prevents submit | Verify HTML5 required | A-LOGIN-002 |
| Empty password prevents submit | Verify HTML5 required | A-LOGIN-003 |
| Loading state during login | Button shows "Signing in...", disabled | A-LOGIN-011 |
| Non-existent user login | Verify error message | A-LOGIN-006 |
| Browser back after login | Verify redirect to dashboard | A-LOGIN-012 |

```
Append to: OAuth2Admin/tests/e2e/applications.spec.ts
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| No grant type selected | Verify error message | A-APP-CR-003 |
| Multiple grant types | Select multiple, verify saved | A-APP-CR-004 |
| Delete client cancel | Cancel confirm, verify client remains | A-APP-005 |

```
Append to: OAuth2Admin/tests/e2e/users.spec.ts
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| Assign multiple roles (comma-separated) | Enter "admin, user", verify both assigned | A-USR-RL-002 |
| Empty role input | Verify no API call | A-USR-RL-003 |
| Non-existent role | Verify error handling | A-USR-RL-005 |

```
Append to: OAuth2Admin/tests/e2e/user-detail.spec.ts
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| Toggle email verified | Toggle checkbox, save, verify updated | A-USR-DT-003 |
| No changes save | Click save without changes, verify message | A-USR-DT-004 |
| Non-existent user ID | Navigate to invalid ID, verify error | A-USR-DT-011 |

```
Append to: OAuth2Admin/tests/e2e/tokens.spec.ts
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| Filter by client_id | Enter filter, verify filtered results | A-TOK-002 |
| Filter by user_id | Enter filter, verify filtered results | A-TOK-003 |
| Clear filters | Click clear, verify all tokens shown | A-TOK-004 |

```
Append to: OAuth2Admin/tests/e2e/roles.spec.ts
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| Duplicate role name | Create role with existing name | A-ROLE-006 |
| Delete role cancel | Cancel confirm dialog | A-ROLE-009 |

#### User Frontend

```
Append to: OAuth2Frontend/tests/e2e/auth.spec.ts (MFA section)
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| Invalid MFA code | Enter wrong code, verify error | U-MFA-003 |
| Less than 6 digits | Verify button disabled | U-MFA-004 |
| Back to login from MFA | Click back, verify login form shown | U-MFA-007 |

```
Append to: OAuth2Frontend/tests/e2e/account.spec.ts (Security section)
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| Password mismatch error | Verify "Passwords do not match" | U-SEC-003 |
| Password too short | Verify "at least 6 characters" | U-SEC-004 |
| Wrong old password | Verify API error shown | U-SEC-005 |
| MFA verify valid code | Complete MFA setup, verify enabled | U-SEC-008 |
| MFA disable with valid password | Disable MFA, verify success | U-SEC-010 |
| Delete account with correct username | Enter matching username, delete | U-SEC-016 |

```
Append to: OAuth2Frontend/tests/e2e/account.spec.ts (Profile section)
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| Email unverified with resend link | Verify yellow badge + resend button | U-PROF-003 |
| Resend verification email | Click resend, verify success | U-PROF-004 |
| Profile API failure | Mock 500, verify error message | U-PROF-007 |

```
Append to: OAuth2Frontend/tests/e2e/navigation.spec.ts
```

| Test Name | Description | Maps To |
|-----------|-------------|---------|
| User dropdown opens/closes | Click avatar, verify dropdown | U-NAV-003 |
| Click outside closes dropdown | Open dropdown, click outside, verify closed | U-NAV-005 |
| Active nav state | Navigate to /security, verify link highlighted | U-NAV-009 |

### 3.3 P2 - Nice to Have (UX & Polish)

#### Admin Console

| Test Name | Module | Maps To |
|-----------|--------|---------|
| Dashboard loading state ("—" placeholder) | Dashboard | A-DASH-004 |
| Failures today > 0 red color | Dashboard | A-DASH-008 |
| Failures today = 0 normal color | Dashboard | A-DASH-007 |
| Success message auto-dismiss (3s) | Cross-cutting | A-ERR-005 |
| Error message auto-dismiss (5s) | Cross-cutting | A-ERR-006 |
| Application detail - no changes save message | Applications | A-APP-DT-003 |
| Token timestamp formatting | Tokens | A-TOK-012 |
| Responsive sidebar layout | Navigation | A-NAV-005 |

#### User Frontend

| Test Name | Module | Maps To |
|-----------|--------|---------|
| Registration success redirect (2s timer) | Registration | U-REG-013 |
| Callback loading spinner | OAuth2 | U-CB-004 |
| Authorized apps empty state | Authorized Apps | U-APP-002 |
| Authorized apps - app without name (fallback) | Authorized Apps | U-APP-006 |
| Authorized apps - success auto-dismiss | Authorized Apps | U-APP-007 |
| Dashboard - no roles "None" display | Dashboard | U-DASH-002 |
| Dashboard - multiple role badges | Dashboard | U-DASH-003 |
| Profile loading state | Profile | U-PROF-008 |
| WebAuthn section hidden when unsupported | Security | U-SEC-015 |

---

## 4. Recommended New Test Files

| File | Target App | Tests | Priority |
|------|-----------|-------|----------|
| `security.spec.ts` | Admin | SQL injection, XSS, token exposure | P0 |
| `error-handling.spec.ts` | Admin | 401/403/500/network errors | P0 |
| `security.spec.ts` | Frontend | SQL injection, XSS, anti-enumeration | P0 |
| `registration-validation.spec.ts` | Frontend | Password rules, duplicate, email validation | P0 |
| `password-reset.spec.ts` | Frontend | Reset token flows | P0 |
| `session-management.spec.ts` | Frontend | Restore, expire, multi-tab | P1 |
| `accessibility.spec.ts` | Frontend | Labels, keyboard nav, ARIA | P2 |

---

## 5. Overall Coverage Summary

| Application | Test Cases (Design) | E2E Tests (Existing) | Missing | Overall E2E Coverage |
|---|---|---|---|---|
| OAuth2Admin | 140 | ~75 (53%) | ~65 | 54% |
| OAuth2Frontend | ~150 | ~36 (24%) | ~114 | 24% |
| **Total** | **~290** | **~111** | **~179** | **38%** |

### Top 3 Gaps by Impact

1. **Frontend Registration & Security (7% and 16% coverage)** - Entire registration validation logic, password change error paths, and MFA verify/disable flows are untested. These are core user-facing flows.

2. **Security Tests (both apps 0-15% coverage)** - No injection/XSS tests exist. These are production-critical for an OAuth2 server.

3. **Error Handling (both apps ~0% coverage)** - No tests for 401/403/500/network errors across either app. Users could see blank pages or crashes in production with zero test coverage.
