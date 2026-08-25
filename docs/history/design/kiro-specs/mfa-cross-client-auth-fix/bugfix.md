# Bugfix Requirements Document

## Introduction

PR #9 (`fix/mfa-verify-issue-token`) changed `MfaController::verifyLogin` to issue real OAuth2 tokens after a successful TOTP check. Security review of that change found two P0 defects that let an attacker who already holds a valid `mfa_token` (which is nothing more than `std::to_string(internalId)`, with no client binding — see `SessionController.cc` ~L426) obtain an OAuth2 token minted for a client that was never involved in the original login:

- **P0-1 — Cross-client authorization confusion**: `verifyLogin` only checks that `client_id`/`redirect_uri` are non-empty. It never verifies the client is registered, never verifies the redirect_uri belongs to that client, and never verifies that the supplied `client_id`/`redirect_uri` match the ones used in the original first-factor `SessionController::login` call that produced the `mfa_token`. An attacker can therefore submit a different, legitimately-registered client's own `client_id` + `redirect_uri` and receive a token bound to that other client.
- **P0-2 — redirect_uri not whitelisted (RFC 6749 §3.1.2.3 violation)**: `verifyLogin`'s call chain (`generateAuthorizationCode` → `exchangeCodeForToken` → `consumeAuthCode`) never validates `redirect_uri` against the target client's registered URIs. The equality check inside `consumeAuthCode` compares two values that both originate from the same single request body in this call path, so it provides no real protection here.

This bugfix adds, inside `verifyLogin`, a same-client/same-redirect_uri binding check against the login session that produced the `mfa_token`, plus registered-client and redirect_uri-whitelist validation (reusing the existing `validateClient`/`validateRedirectUri` pattern already used by `OAuth2StandardController::authorize` and `DeviceAuthController::deviceAuthorization`). All new rejections use the existing `AUTH_INVALID_CREDENTIALS` (HTTP 401) error code so that, from the outside, an invalid MFA session, an unknown client, a non-whitelisted redirect_uri, a mismatched login-session binding, and a wrong TOTP code all remain indistinguishable (no oracle for client registration).

**Explicitly out of scope for this fix** (do not address, do not silently drop — carried forward unchanged):
- No change to `mfa_token`'s format, lifetime, or randomness. It remains `std::to_string(internalId)`. As a known, pre-existing, accepted limitation: `mfa_token` has no expiry or randomness of its own, so concurrent login attempts by the same user will overwrite each other's stored pending client/redirect_uri binding. This is unrelated to P0-1/P0-2 and is not fixed here.
- No scope validation changes in `verifyLogin` (RBAC reads DB user roles, not token scope, so scope spoofing is not exploitable through this endpoint).
- No PKCE addition to this MFA second-factor leg (tracked separately).
- No changes to `TokenService`/`consumeAuthCode`'s existing redirect_uri comparison logic (it is correct for the general authorization_code grant; the gap was only that `verifyLogin` never invoked the whitelist pre-check).

## Bug Analysis

### Current Behavior (Defect)

1.1 WHEN `verifyLogin` receives a valid `mfa_token`, a correct TOTP `code`, and a `client_id` that is NOT a registered client THEN the system proceeds to generate an authorization code and issue tokens anyway, with no rejection.

1.2 WHEN `verifyLogin` receives a valid `mfa_token`, a correct TOTP `code`, a registered `client_id`, and a `redirect_uri` that is NOT in that client's registered redirect_uri whitelist THEN the system proceeds to generate an authorization code and issue tokens anyway, with no rejection.

1.3 WHEN `verifyLogin` receives a valid `mfa_token`, a correct TOTP `code`, a registered `client_id`, and a `redirect_uri` that IS in that client's whitelist, but that `client_id`/`redirect_uri` pair differs from the `client_id`/`redirect_uri` used in the original first-factor `SessionController::login` call that produced the `mfa_token` THEN the system issues an OAuth2 token bound to the different client, with no rejection (cross-client authorization confusion).

1.4 WHEN `TokenService::exchangeCodeForToken` → `consumeAuthCode` performs its redirect_uri equality check inside the internal `verifyLogin` flow THEN both compared values originate from the same single request body, so the check cannot detect an unregistered or non-whitelisted redirect_uri (self-referential, not a real security control in this call path).

### Expected Behavior (Correct)

2.1 WHEN `verifyLogin` receives a `client_id` that is NOT a registered client THEN the system SHALL reject the request with `AUTH_INVALID_CREDENTIALS` (HTTP 401) and SHALL NOT generate an authorization code or issue tokens.

2.2 WHEN `verifyLogin` receives a registered `client_id` but a `redirect_uri` that is NOT in that client's registered whitelist THEN the system SHALL reject the request with `AUTH_INVALID_CREDENTIALS` (HTTP 401) and SHALL NOT generate an authorization code or issue tokens.

2.3 WHEN `verifyLogin` receives a `client_id`/`redirect_uri` pair that does not match the `mfa_pending_client_id`/`mfa_pending_redirect_uri` recorded for that user during the original first-factor `SessionController::login` call THEN the system SHALL reject the request with `AUTH_INVALID_CREDENTIALS` (HTTP 401), with a message indicating the client/redirect_uri does not match the login session, and SHALL NOT generate an authorization code or issue tokens — even when that `client_id` is independently registered and that `redirect_uri` is independently whitelisted for it.

2.4 WHEN `verifyLogin` receives a correct TOTP `code`, a `client_id`/`redirect_uri` pair matching the pending first-factor login session, a registered `client_id`, and a whitelisted `redirect_uri` for that client THEN the system SHALL proceed to generate an authorization code, exchange it for tokens, and return them bound to that client, exactly as today.

2.5 WHEN `verifyLogin` completes successfully (tokens issued per 2.4) THEN the system SHALL clear the stored `mfa_pending_client_id` and `mfa_pending_redirect_uri` for that user back to NULL, so the binding cannot be reused by a later, unrelated verification attempt.

2.6 WHEN `SessionController::login` determines that MFA is required (`authResult->mfaEnabled` is true) THEN the system SHALL persist the current login request's `client_id` and `redirect_uri` into `mfa_pending_client_id`/`mfa_pending_redirect_uri` for that user row before returning the `mfa_required` response, so that a later `verifyLogin` call has a session binding to check against.

### Unchanged Behavior (Regression Prevention)

3.1 WHEN `SessionController::login` authenticates a user who does NOT have MFA enabled THEN the system SHALL CONTINUE TO generate an authorization code and respond exactly as it does today (no pending-column writes, no new validation on this path).

3.2 WHEN `SessionController::login` returns the `mfa_required` response for a user with MFA enabled THEN the system SHALL CONTINUE TO return `mfa_token` as `std::to_string(internalId)`, unchanged in format.

3.3 WHEN `verifyLogin` receives an incorrect TOTP `code` (regardless of client/redirect_uri validity) THEN the system SHALL CONTINUE TO reject with `AUTH_INVALID_CREDENTIALS` ("verifyLogin: TOTP code is incorrect").

3.4 WHEN `verifyLogin` receives an `mfa_token` that does not correspond to any existing user id THEN the system SHALL CONTINUE TO reject with `AUTH_INVALID_CREDENTIALS` ("verifyLogin: invalid MFA session").

3.5 WHEN `verifyLogin` receives a request missing `mfa_token`, `code`, `client_id`, or `redirect_uri` THEN the system SHALL CONTINUE TO reject with `VALIDATION_MISSING_REQUIRED_FIELD`, unchanged from today's behavior.

3.6 WHEN `OAuth2StandardController::authorize` or `DeviceAuthController::deviceAuthorization` perform their existing client/redirect_uri validation chains THEN the system SHALL CONTINUE TO behave exactly as it does today (this fix reuses, but does not modify, `ClientService::validateClient`/`validateRedirectUri`).

3.7 WHEN `TokenService::exchangeCodeForToken`/`consumeAuthCode` validate `redirect_uri` equality for the general authorization_code grant flow (outside the `verifyLogin` internal call path) THEN the system SHALL CONTINUE TO behave exactly as it does today.

3.8 WHEN `AuthorizationFilter`/RBAC authorizes access to protected endpoints THEN the system SHALL CONTINUE TO read roles from the database rather than from token scope, unaffected by this fix (scope validation remains intentionally out of scope for `verifyLogin`).

3.9 WHEN a client's `client_id` and `redirect_uri` are correctly registered and match both the whitelist and the first-factor login session THEN the system SHALL CONTINUE TO issue `access_token`/`refresh_token` in the same response shape as today (`mfa_verified: true`, `message: "MFA verification successful"`, plus the token fields).
