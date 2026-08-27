# OAuth2 Admin Console - Test Cases

> Admin backend-path: `/admin/` | Framework: Vue 3 + TailwindCSS | Playwright E2E

## Module 1: Login / Authentication

### 1.1 Admin Login Page (`/admin/login`)

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-LOGIN-001 | Valid admin credentials | Enter valid admin username/password, click Sign in | Redirect to Dashboard (`/admin/`) | P0 |
| A-LOGIN-002 | Empty username | Leave username blank, click Sign in | Form does not submit (HTML5 required) | P1 |
| A-LOGIN-003 | Empty password | Leave password blank, click Sign in | Form does not submit (HTML5 required) | P1 |
| A-LOGIN-004 | Both fields empty | Click Sign in with empty fields | Form does not submit | P1 |
| A-LOGIN-005 | Wrong password | Enter valid username + wrong password | Red error banner displayed, stays on login page | P0 |
| A-LOGIN-006 | Non-existent user | Enter unregistered username + any password | Error message shown | P0 |
| A-LOGIN-007 | Non-admin user | Enter credentials of user without admin role | Error message: requires admin role | P0 |
| A-LOGIN-008 | SQL injection in username | Enter `' OR 1=1 --` as username | Error message, no data leak | P0 |
| A-LOGIN-009 | XSS in username | Enter `<script>alert('xss')</script>` | Input rendered as text, no script execution | P0 |
| A-LOGIN-010 | Whitespace-only username | Enter spaces only, click Sign in | Form does not submit | P1 |
| A-LOGIN-011 | Loading state | Submit valid credentials | Button shows "Signing in...", disabled during request | P2 |
| A-LOGIN-012 | Browser back after login | Login successfully, press browser Back | Redirect to Dashboard (auth guard active) | P1 |
| A-LOGIN-013 | Direct access to protected page | Navigate to `/admin/users` without auth | Redirect to `/admin/login` | P0 |
| A-LOGIN-014 | Session persistence | Login, close tab, reopen `/admin/` | Session restored, Dashboard shown | P1 |
| A-LOGIN-015 | Concurrent login attempts | Rapidly click Sign in multiple times | Only one request sent (button disabled) | P2 |

### 1.2 Logout

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-LOGOUT-001 | Normal logout | Click "Sign out" in sidebar | Session cleared, redirect to login page | P0 |
| A-LOGOUT-002 | Access after logout | Logout, navigate to `/admin/users` | Redirect to login page | P0 |
| A-LOGOUT-003 | Browser back after logout | Logout, press browser Back | Redirect to login (no cached page) | P1 |

---

## Module 2: Dashboard (`/admin/`)

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-DASH-001 | Stats display on load | Navigate to Dashboard | 4 stat cards shown: Total Users, Applications, Active Tokens, Failures Today | P0 |
| A-DASH-002 | System health indicators | View Dashboard | System Status (green/red dot), Database status, Redis status displayed | P0 |
| A-DASH-003 | Quick action links | Click each Quick Action card | Navigates to correct page (Applications/Users/Roles/Scopes) | P1 |
| A-DASH-004 | Loading state | Observe page during API call | Stats show "—" while loading, then update | P2 |
| A-DASH-005 | API failure handling | Simulate `/health/ready` failure | Red error banner shown, System Status shows "Unhealthy" | P0 |
| A-DASH-006 | Stats API failure | Simulate `/api/admin/dashboard/stats` failure | Error banner displayed with descriptive message | P0 |
| A-DASH-007 | Failures today = 0 | When no failures today | Number displayed in normal text color (not red) | P2 |
| A-DASH-008 | Failures today > 0 | When failures exist | Number displayed in red color | P2 |

---

## Module 3: Applications (`/admin/applications`)

### 3.1 Application List

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-APP-001 | List loads successfully | Navigate to Applications page | Table shows client_id, name, type, grant_types, created_at | P0 |
| A-APP-002 | Empty list | When no clients exist | Table shows "No clients found" or empty state | P1 |
| A-APP-003 | Navigate to detail | Click a client row | Navigates to `/admin/applications/:id` | P0 |
| A-APP-004 | Delete client | Click Delete on a client, confirm dialog | Client removed from list, success message | P0 |
| A-APP-005 | Delete client cancel | Click Delete, cancel confirm dialog | Client remains in list | P1 |
| A-APP-006 | Reset secret from list | Click Reset Secret, confirm | Secret modal shows new secret | P0 |

### 3.2 Create Application

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-APP-CR-001 | Create CONFIDENTIAL client | Fill name, type=CONFIDENTIAL, redirect URIs, select grant types, submit | Client created, secret modal shown with new secret | P0 |
| A-APP-CR-002 | Create with empty name | Leave name empty, submit | Form validation prevents submission or API error shown | P0 |
| A-APP-CR-003 | No grant type selected | Deselect all grant types, submit | Error: "Please select at least one grant type" | P0 |
| A-APP-CR-004 | Multiple grant types | Select authorization_code + refresh_token + client_credentials | Client created with comma-separated grant types | P1 |
| A-APP-CR-005 | Duplicate client name | Create two clients with same name | Second creation succeeds (name is not unique key) or proper error | P1 |
| A-APP-CR-006 | Very long redirect URI | Enter redirect URI > 2048 chars | Either succeeds or server returns validation error gracefully | P2 |
| A-APP-CR-007 | Invalid redirect URI format | Enter `not-a-url` as redirect URI | Validation error shown | P1 |
| A-APP-CR-008 | Device code grant type | Select `urn:ietf:params:oauth:grant-type:device_code` | Client created with device_code grant | P1 |
| A-APP-CR-009 | Close modal without submit | Open create modal, click Cancel | Modal closes, no API call | P1 |
| A-APP-CR-010 | Loading state during create | Submit create form | Button shows "Creating...", disabled | P2 |

### 3.3 Application Detail (`/admin/applications/:id`)

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-APP-DT-001 | Info tab loads | Open application detail | Client name, redirect URIs, grant types displayed in editable form | P0 |
| A-APP-DT-002 | Save name change | Edit client name, click Save | Success message, name updated | P0 |
| A-APP-DT-003 | Save with no changes | Click Save without modifying anything | Message: "No changes to save" | P1 |
| A-APP-DT-004 | Edit redirect URIs | Change redirect URIs (multi-line), save | URIs saved as comma-separated, displayed correctly on reload | P0 |
| A-APP-DT-005 | Invalid application ID | Navigate to `/admin/applications/non-existent-id` | Error message displayed | P1 |
| A-APP-DT-006 | Scopes tab | Switch to Scopes tab | Available scopes shown with checkboxes, current scopes checked | P0 |
| A-APP-DT-007 | Save scopes | Check/uncheck scopes, click Save | Scopes updated, success message | P0 |
| A-APP-DT-008 | Reset secret | Click Reset Secret, confirm | New secret shown in modal | P0 |
| A-APP-DT-009 | Copy to clipboard | Click copy button for secret | Secret copied, "Copied to clipboard" message | P2 |
| A-APP-DT-010 | Credentials tab | Switch to Credentials tab | Client ID displayed, reset secret option available | P1 |

---

## Module 4: Users (`/admin/users`)

### 4.1 User List

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-USR-001 | List loads | Navigate to Users page | Table with Username, Email, Verified, MFA, Actions columns | P0 |
| A-USR-002 | Verified badge | User has email_verified=true | Green "Verified" badge shown | P1 |
| A-USR-003 | Unverified badge | User has email_verified=false | Yellow "Pending" badge shown | P1 |
| A-USR-004 | MFA enabled | User has mfa_enabled=true | Green "Enabled" badge | P1 |
| A-USR-005 | MFA disabled | User has mfa_enabled=false | Gray "Off" badge | P1 |
| A-USR-006 | Navigate to user detail | Click "Details" link | Navigates to `/admin/users/:id` | P0 |
| A-USR-007 | API error | Simulate GET /api/admin/users failure | Error banner displayed | P0 |

### 4.2 Role Assignment (List Page Modal)

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-USR-RL-001 | Assign single role | Click "Assign Roles", enter "admin", save | Roles updated, modal closes | P0 |
| A-USR-RL-002 | Assign multiple roles | Enter "admin, user" (comma-separated) | Both roles assigned | P0 |
| A-USR-RL-003 | Empty role input | Click save with empty role input | No API call (button disabled or input validation) | P1 |
| A-USR-RL-004 | Whitespace roles | Enter ", , admin, " | Only "admin" assigned (trimmed, filtered) | P1 |
| A-USR-RL-005 | Non-existent role | Enter "superadmin" | API error or graceful handling | P1 |
| A-USR-RL-006 | Cancel role assignment | Open modal, click Cancel | Modal closes, no changes | P1 |
| A-USR-RL-007 | Loading state | Submit role assignment | Button shows "Saving...", disabled | P2 |

### 4.3 User Detail (`/admin/users/:id`)

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-USR-DT-001 | Info tab loads | Open user detail | Username, email, email_verified shown in editable form | P0 |
| A-USR-DT-002 | Edit email | Change email, save | Email updated, success message | P0 |
| A-USR-DT-003 | Toggle email verified | Toggle verified checkbox, save | Verification status updated | P0 |
| A-USR-DT-004 | No changes save | Click Save without changes | "No changes" message | P1 |
| A-USR-DT-005 | Roles tab | Switch to Roles tab | Available roles as checkboxes, current roles selected | P0 |
| A-USR-DT-006 | Save roles | Check/uncheck roles, save | Roles updated, success message | P0 |
| A-USR-DT-007 | Disable user | Click "Disable User", confirm dialog | User disabled, status updated | P0 |
| A-USR-DT-008 | Enable user | Click "Enable User" | User enabled, status updated | P0 |
| A-USR-DT-009 | Security tab | Switch to Security tab | Lock status, login attempts, locked_until shown | P1 |
| A-USR-DT-010 | Locked user indicator | View locked user | "Locked" status with remaining time shown | P1 |
| A-USR-DT-011 | Non-existent user | Navigate to `/admin/users/999999` | Error message displayed | P1 |
| A-USR-DT-012 | Concurrent role edit | Two admins edit same user's roles simultaneously | Last write wins or conflict error | P2 |

---

## Module 5: Roles (`/admin/roles`)

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-ROLE-001 | List roles | Navigate to Roles page | Table with Name, Description, Users count, Actions | P0 |
| A-ROLE-002 | Built-in role indicators | View admin/user roles | "built-in" badge shown | P1 |
| A-ROLE-003 | Built-in roles cannot be deleted | View actions for built-in roles | No "Delete" button shown | P0 |
| A-ROLE-004 | Create role | Click Create, enter name + description, submit | Role created, appears in table | P0 |
| A-ROLE-005 | Create role empty name | Submit with empty name | Button disabled (form validation) | P0 |
| A-ROLE-006 | Create duplicate role name | Create role with existing name | API error shown: role already exists | P0 |
| A-ROLE-007 | Edit role description | Click Edit, change description, save | Description updated | P0 |
| A-ROLE-008 | Delete custom role | Click Delete on custom role, confirm | Role deleted, removed from table | P0 |
| A-ROLE-009 | Delete role cancel | Click Delete, cancel confirm dialog | Role remains | P1 |
| A-ROLE-010 | Role with assigned users | Delete a role that has users assigned | Confirm dialog appears; after deletion, users lose that role | P1 |
| A-ROLE-011 | Empty role list | When no custom roles exist | Only built-in roles shown (admin, user) | P2 |
| A-ROLE-012 | XSS in role name | Enter `<script>alert(1)</script>` as name | Name rendered as text, no script execution | P0 |
| A-ROLE-013 | Very long role name | Enter name > 100 chars | Either succeeds or proper validation error | P2 |

---

## Module 6: Scopes (`/admin/scopes`)

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-SCP-001 | List scopes | Navigate to Scopes page | Table with Name, Description, Mapped Role, Default, Admin-only, Actions | P0 |
| A-SCP-002 | Built-in scope indicators | View openid/profile/email/admin | "built-in" badge or non-deletable | P1 |
| A-SCP-003 | Create scope | Fill name, description, mapped_role, toggle is_default/requires_admin_role, submit | Scope created, appears in table | P0 |
| A-SCP-004 | Create scope empty name | Submit with empty name | Button disabled or validation error | P0 |
| A-SCP-005 | Create duplicate scope | Create scope with existing name | API error: scope already exists | P0 |
| A-SCP-006 | Edit scope | Click Edit, change description/mapped_role/toggles, save | Scope updated | P0 |
| A-SCP-007 | Toggle is_default | Set is_default=true for a scope | Scope marked as default | P1 |
| A-SCP-008 | Toggle requires_admin_role | Set requires_admin_role=true | Scope marked as admin-only | P1 |
| A-SCP-009 | Delete scope | Click Delete on custom scope, confirm | Scope deleted | P0 |
| A-SCP-010 | Delete built-in scope | Try to delete openid/admin | Delete button not shown or error | P0 |
| A-SCP-011 | XSS in scope name | Enter `<img onerror=alert(1) src=x>` as name | Rendered as text | P0 |

---

## Module 7: Tokens (`/admin/tokens`)

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-TOK-001 | List tokens | Navigate to Tokens page | Table with token_prefix, client_id, user_id, scope, created_at, expires_at | P0 |
| A-TOK-002 | Filter by client_id | Enter client_id filter, click Apply | Only tokens for that client shown | P0 |
| A-TOK-003 | Filter by user_id | Enter user_id filter, click Apply | Only tokens for that user shown | P0 |
| A-TOK-004 | Clear filters | Click "Clear Filters" | Filters reset, all tokens shown | P1 |
| A-TOK-005 | Revoke single token | Click Revoke on a token, confirm dialog | Token revoked, removed from list | P0 |
| A-TOK-006 | Revoke by client | Click bulk action "Revoke by Client", confirm | All tokens for that client revoked | P0 |
| A-TOK-007 | Revoke by user | Click "Revoke by User" (requires user_id filter), confirm | All tokens for that user revoked | P0 |
| A-TOK-008 | Revoke by user without filter | Click "Revoke by User" without user_id filter | No action (guard: `if (!userIdFilter.value) return`) | P1 |
| A-TOK-009 | Pagination | When tokens > per_page (50) | Page navigation works, page parameter sent in API | P1 |
| A-TOK-010 | Empty token list | When no tokens exist | Table shows empty state | P2 |
| A-TOK-011 | Confirm cancel | Click Revoke, cancel confirm dialog | Token not revoked | P1 |
| A-TOK-012 | Timestamp formatting | Verify created_at/expires_at display | Formatted as locale string, not raw ISO | P2 |

---

## Module 8: Audit Logs (`/admin/logs`)

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-LOG-001 | List loads | Navigate to Audit Logs page | Table with timestamp, action, user, details | P0 |
| A-LOG-002 | Empty logs | When no audit logs exist | Empty state displayed | P1 |
| A-LOG-003 | Pagination | When logs > per_page | Pagination controls work | P1 |
| A-LOG-004 | Filter by action type | Filter by specific action | Filtered results shown | P1 |
| A-LOG-005 | Timestamp ordering | View multiple logs | Sorted by timestamp descending (newest first) | P1 |

---

## Module 9: Settings (`/admin/settings`)

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-SET-001 | Page loads | Navigate to Settings page | Current settings displayed in editable form | P0 |
| A-SET-002 | Save settings | Modify setting value, click Save | Success message, settings updated | P0 |
| A-SET-003 | Invalid setting value | Enter invalid value | Validation error shown | P1 |
| A-SET-004 | No changes save | Click Save without changes | "No changes" or settings re-fetched | P2 |

---

## Module 10: Navigation & Layout

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-NAV-001 | Sidebar navigation | Click each nav item | Correct page loads, active item highlighted | P0 |
| A-NAV-002 | Active state on detail pages | Navigate to `/admin/applications/:id` | "Applications" nav item highlighted | P1 |
| A-NAV-003 | Top bar title | Navigate between pages | Top bar shows current page name | P2 |
| A-NAV-004 | User info in sidebar | After login | Username initial avatar, name, email shown | P2 |
| A-NAV-005 | Responsive layout | Resize to mobile width | Sidebar collapses or becomes hamburger menu | P1 |

---

## Module 11: Cross-Cutting Concerns

### 11.1 Error Handling

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-ERR-001 | Network error | Disable network during API call | Error banner with user-friendly message | P0 |
| A-ERR-002 | 401 Unauthorized | Let session expire, make API call | Redirect to login page | P0 |
| A-ERR-003 | 403 Forbidden | Admin-only operation by non-admin | Error message, no data exposed | P0 |
| A-ERR-004 | 500 Server error | Trigger server error | Error banner, no crash | P0 |
| A-ERR-005 | Success message auto-dismiss | Perform successful action | Success message disappears after 3 seconds | P2 |
| A-ERR-006 | Error message auto-dismiss | Trigger error message | Error message disappears after 5 seconds | P2 |

### 11.2 Security

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-SEC-001 | CSRF protection (CORS same-origin) | Submit forms / send API requests | Bearer-token architecture: auth token is NOT in a cookie, so classic CSRF does not apply. Protection is enforced by backend CORS with strict exact-match Origin allowlist (no wildcards) — `main.cc` origin check. Cross-origin requests from non-allowlisted origins are rejected | P0 |
| A-SEC-002 | Token storage | After login | Auth token not in URL or localStorage in plaintext | P0 |
| A-SEC-003 | Route guard bypass | Manually enter `/admin/users` URL without auth | Redirected to login | P0 |
| A-SEC-004 | Client secret display | View/create client secret | Secret shown only once, not persisted in page state | P0 |

### 11.3 Performance

| ID | Test Case | Steps | Expected Result | Priority |
|----|-----------|-------|-----------------|----------|
| A-PERF-001 | Large user list | Load 1000+ users | Page renders without freezing, pagination working | P1 |
| A-PERF-002 | Dashboard concurrent requests | Load Dashboard | Two API calls fire in parallel (Promise.all) | P2 |
| A-PERF-003 | Lazy-loaded routes | Navigate to each page | Only required component loaded (code splitting) | P2 |
