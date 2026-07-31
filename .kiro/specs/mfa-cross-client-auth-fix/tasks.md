# Implementation Plan

## Overview

This plan implements the fix specified in `design.md` for the MFA cross-client authorization
confusion bug (P0-1/P0-2). It follows the bug condition methodology's two-phase approach:

1. **Explore** — write tests against **unfixed** code (F) that surface counterexamples confirming
   the bug exists (Bug Condition).
2. **Preserve** — write tests against **unfixed** code (F) that pin the baseline behavior that
   must survive unchanged (Preservation Requirements).
3. **Implement** — apply the schema migration and both code changes from `design.md`'s Fix
   Implementation section.
4. **Validate** — re-run the exploration/preservation tests against **fixed** code (F'), add
   property-based fix-check tests, integration tests, and a full regression pass.

**Correctness Properties (design.md, single source of truth)**:
- **Property 1 — Bug Condition: Unregistered client rejected** — validates 2.1
- **Property 2 — Bug Condition: Non-whitelisted redirect_uri rejected** — validates 2.2
- **Property 3 — Bug Condition: Cross-client / mismatched pending-binding rejected** — validates 2.3
- **Property 4 — Preservation: Matching binding still issues tokens** — validates 2.4, 3.9
- **Property 5 — Bug Condition: Pending binding cleared after success** — validates 2.5
- **Property 6 — Bug Condition: Login persists pending binding when MFA required** — validates 2.6
- **Property 7 — Preservation: Non-MFA login and unrelated verifyLogin rejections unchanged** — validates 3.1-3.8

**Build/verify tooling (existing project conventions)**: CMake + the project's standard backend
build scripts. Test target `OAuth2Test_test` (`ctest` name `OAuth2Tests`). New test sources are
placed under `OAuth2Server/test/integration/auth/`, already collected by the `GLOB_RECURSE
INTEGRATION_TESTS` glob in `OAuth2Server/test/CMakeLists.txt` — no build-file changes needed.
Tests use the project's `DROGON_TEST` naming convention
`[Category]_[Priority]_[Module]_[Feature]_[Scenario]` with the `PropertyN` token in the
Feature slot, e.g. `Integration_P1_MfaCrossClientAuthFix_Property1_UnregisteredClient`
(see `tests/integration/auth/Property1_MfaCrossClientAuthFix_ExploratoryTest.cc`; enforced
by `tools/test/scripts/naming_validator.sh`).

## Task Dependency Graph

```
                          [1] Schema migration (V022)
                                        │
                ┌───────────────────────┴───────────────────────┐
                ▼                                                 ▼
      [2] Exploratory bug-condition               [3] Preservation property tests
          tests (unfixed code)                        (unfixed code)
          Property 1: Bug Condition                   Property 2: Preservation
                │                                                 │
                └───────────────────────┬───────────────────────┘
                                        ▼
                          [4] Fix: implement changes
                       4.1 SessionController::login (persist binding)
                       4.2 MfaController::verifyLogin (validate + clear binding)
                       4.3 re-run Task 2 tests -> now PASS (Property 1: Expected Behavior)
                       4.4 re-run Task 3 tests -> still PASS (Property 2: Preservation)
                                        │
        ┌───────────────────────────────┼───────────────────────────────┐
        ▼                               ▼                               ▼
 [5] Fix-check PBTs             [6] Preservation/regression       [7] Integration tests
   Properties 1,2,3,5,6            tests, Property 7                 Properties 3,4,5,6
        │                               │                               │
        └───────────────────────────────┴───────────────────────────────┘
                                        ▼
                    [8] Checkpoint: full build + existing regression suite
```

Execution waves (tasks within a wave can run in parallel; waves run in order):

```json
{
  "waves": [
    {
      "wave": 1,
      "description": "Schema migration - adds the two pending-binding columns everything else reads/writes",
      "tasks": ["1"]
    },
    {
      "wave": 2,
      "description": "Reproduce the bug and capture the preservation baseline on unfixed code (both can run in parallel; both depend only on the migration)",
      "tasks": ["2", "3"]
    },
    {
      "wave": 3,
      "description": "Implement the fix: SessionController::login and MfaController::verifyLogin changes, then re-verify the exploration/preservation tests from wave 2",
      "tasks": ["4.1", "4.2", "4.3", "4.4"]
    },
    {
      "wave": 4,
      "description": "Fix-check PBTs, preservation/regression tests, and integration tests - independent test surfaces, all depend on the fix being implemented",
      "tasks": ["5", "6", "7"]
    },
    {
      "wave": 5,
      "description": "Final checkpoint: full build and existing regression suite",
      "tasks": ["8"]
    }
  ]
}
```

**Key dependencies**:

- Task 1 (migration) is a hard prerequisite for everything else — the pending-binding columns
  must exist before Task 2's exploratory tests can even query them, and before Tasks 4.1/4.2 can
  read/write them.
- Tasks 2 and 3 are independent of each other (different test files, different concerns) and both
  depend only on Task 1. Both **must** run against unfixed code to have value — Task 2 needs to
  observe tokens being incorrectly issued; Task 3 needs to observe the legitimate baseline before
  anything changes.
- Tasks 4.1 and 4.2 touch different files/functions (`SessionController.cc` vs `MfaController.cc`)
  and can be implemented in parallel, but both should land before 4.3/4.4 re-verification, since
  the full fix (persist binding in 4.1 + validate/clear binding in 4.2) is only observable
  end-to-end once both are in place.
- Tasks 5, 6, 7 can be written/run in parallel once Task 4 is complete, since they exercise
  different test surfaces (randomized fix-check PBTs, preservation regressions, and end-to-end
  integration flows respectively).
- Task 8 is the sink: it depends on every other task and gates completion.

## Tasks

### Phase 0: Schema

- [ ] 1. Schema migration: add MFA pending client/redirect_uri binding columns
  - Create `OAuth2Server/sql/migrations/V022__mfa_pending_client_binding.sql`, the next number
    after `V021__widen_email_verification_tokens_email.sql`
  - Use the exact idempotent SQL from design.md's "Fix Implementation #1":
    ```sql
    ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_pending_client_id VARCHAR(50);
    ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_pending_redirect_uri TEXT;
    ```
  - No `DOWN`/rollback section (forward-only migrations, consistent with V001-V021)
  - Apply the migration locally (`SchemaManager` auto-applies on next server/test start) and
    confirm both columns exist as nullable with no default, matching `mfa_secret`'s pattern
  - _Requirements: 2.6, 2.3 (columns are the substrate all other requirements depend on)_

### Phase 1: Bug reproduction and baseline capture (on unfixed code F, before any fix)

- [ ] 2. Write bug condition exploration tests
  - **Property 1: Bug Condition** - Cross-client auth confusion (unregistered client, non-whitelisted redirect_uri, cross-client confusion)
  - **IMPORTANT**: Write and run these tests BEFORE implementing Task 4 (the fix). Only Task 1
    (migration) may already be applied — the pending columns will exist but nothing yet writes or
    reads them, so `mfa_pending_client_id`/`mfa_pending_redirect_uri` are `NULL` for every user at
    this point. That is expected and consistent with test case 4 below.
  - **GOAL**: Surface counterexamples confirming `design.md`'s Bug Condition
    (`isBugCondition(input)`): a valid `mfa_token` + correct TOTP `code`, combined with an
    unregistered client, a non-whitelisted redirect_uri, or (once bindings exist) a mismatched
    pending binding, currently results in tokens being issued anyway.
  - Create `OAuth2Server/test/integration/auth/Property1_MfaCrossClientAuthFix_ExploratoryTest.cc`
  - Seed a test DB with two registered clients (`vue-client` /
    `http://localhost:5173/callback` and `admin-console` /
    `http://localhost:5173/admin/callback`) and a user with MFA enabled
  - **Test Case 1 (Unregistered client)**: log in to get `mfa_token`, call `verifyLogin` with
    `client_id` not present in `oauth2_clients` — run on unfixed code, **expect tokens issued**
    (the counterexample)
  - **Test Case 2 (Non-whitelisted redirect_uri)**: `client_id=vue-client` (registered),
    `redirect_uri` not in `vue-client`'s whitelist — run on unfixed code, **expect tokens issued**
  - **Test Case 3 (Cross-client confusion)**: log in as `vue-client`, then `verifyLogin` with
    `admin-console`'s own valid registered `client_id`/`redirect_uri` — run on unfixed code,
    **expect tokens issued, bound to `admin-console`** (not `vue-client`)
  - **Test Case 4 (Pending-binding-absent edge case)**: `verifyLogin` called while
    `mfa_pending_client_id` is still `NULL` (true for every row until Task 4 lands) — document
    this is currently unchecked entirely (no rejection) on unfixed code
  - **EXPECTED OUTCOME**: All 4 cases confirm the bug — tokens are issued when they should be
    rejected. Do NOT attempt to fix the code at this point.
  - Document each counterexample (request params, observed response, token/client mismatch) in
    the test file's comments to confirm the root cause hypothesis in design.md's "Hypothesized
    Root Cause" section
  - _Requirements: 1.1, 1.2, 1.3, 1.4_

- [ ] 3. Write preservation property tests (BEFORE implementing the fix)
  - **Property 2: Preservation** - Legitimate matching-binding path and unrelated rejections
  - **IMPORTANT**: Follow observation-first methodology — run against UNFIXED code first
  - Create `OAuth2Server/test/integration/auth/Property7_MfaCrossClientAuthFix_PreservationTest.cc`
  - **Observe (matching-binding path)**: `verifyLogin` with `client_id`/`redirect_uri` that are
    registered, whitelisted, AND equal (both still `NULL`/absent binding at this point, or
    manually matched) issues tokens with response shape `{mfa_verified: true, message: "MFA
    verification successful", access_token, refresh_token, ...}` on unfixed code
  - **Observe (wrong TOTP)**: `verifyLogin` with an incorrect TOTP `code` (any client/redirect_uri)
    returns `AUTH_INVALID_CREDENTIALS` / "verifyLogin: TOTP code is incorrect" on unfixed code
  - **Observe (missing fields)**: `verifyLogin` missing `mfa_token`/`code`/`client_id`/
    `redirect_uri` returns `VALIDATION_MISSING_REQUIRED_FIELD` on unfixed code
  - **Observe (unknown mfa_token)**: `verifyLogin` with an `mfa_token` not resolving to any user id
    returns `AUTH_INVALID_CREDENTIALS` / "verifyLogin: invalid MFA session" on unfixed code
  - **Observe (non-MFA login)**: `SessionController::login` for a user without MFA enabled
    performs no pending-column writes and returns its existing authorization-code response
    unchanged on unfixed code
  - Write property-based tests (randomized client/redirect_uri combinations, randomized malformed
    request bodies) capturing these five observed behavior patterns from the Preservation
    Requirements section of design.md
  - **EXPECTED OUTCOME**: All property tests PASS on UNFIXED code (this is the baseline to
    preserve, not a bug — do not "fix" anything if these pass)
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.9_

### Phase 2: Fix implementation

- [ ] 4. Fix: persist and validate the MFA pending client/redirect_uri binding

  - [ ] 4.1 Implement `SessionController::login` pending-binding persistence
    - In the `authResult->mfaEnabled` branch (currently `SessionController.cc` ~420-431),
      convert the branch's `callback` capture to the `sharedCb` /
      `std::make_shared<std::function<void(const HttpResponsePtr &)>>` idiom (matching
      `MfaController`'s existing pattern), local to this branch only
    - Insert a new async `db->execSqlAsync` `UPDATE users SET mfa_pending_client_id = $1,
      mfa_pending_redirect_uri = $2 WHERE id = $3` (bound to `clientId`, `redirectUri`,
      `authResult->internalId`) BEFORE building/sending the `mfa_required` response
    - On success: build and send the existing `mfa_required` JSON response unchanged
      (`mfa_required: true`, `mfa_token`, `message`)
    - On failure: respond with `DB_QUERY_ERROR` (fail-closed) and do NOT send `mfa_required` —
      per design.md's rationale, a stale/wrong pending binding must never be left in place
    - Capture `authResult->internalId` by value (as `internalId`) rather than the outer
      `authResult` optional, to avoid a lifetime dependency across the async hop
    - _Bug_Condition: isBugCondition(input) — missing pending-binding persistence at login time_
    - _Expected_Behavior: Requirement 2.6 — persist client_id/redirect_uri before mfa_required is sent_
    - _Preservation: Requirement 3.1, 3.2 — non-MFA login path and mfa_token format unchanged_
    - _Requirements: 2.6, 3.1, 3.2_

  - [ ] 4.2 Implement `MfaController::verifyLogin` restructuring
    - Move the `plugin = drogon::app().getPlugin<OAuth2Plugin>()` lookup (currently inside the
      TOTP-success branch, `MfaController.cc` ~350-357) up to immediately after the
      `sharedCb` is created, deleting the now-duplicate lookup from the TOTP-success branch
    - Extend the SELECT to `SELECT id, public_sub, mfa_secret, mfa_backup_codes,
      mfa_pending_client_id, mfa_pending_redirect_uri FROM users WHERE id = $1`
    - Preserve the existing TOTP check ordering (checked immediately after the SELECT, before
      any new checks) per design.md's note on TOTP-check ordering
    - After the TOTP check passes, insert three new checks in order, all rejecting with
      `AUTH_INVALID_CREDENTIALS` (401):
      1. `plugin->validateClient(clientId, "", cb)` → reject "verifyLogin: unknown or invalid
         client" on failure
      2. `plugin->validateRedirectUri(clientId, redirectUri, cb)` → reject "verifyLogin:
         redirect_uri not registered for client" on failure
      3. `(clientId, redirectUri) != (pendingClientId, pendingRedirectUri)` → reject
         "verifyLogin: client/redirect_uri does not match login session" (treat `NULL` pending
         values as a mismatch against any non-null clientId, per design.md's edge case)
    - Thread `plugin` through every subsequent lambda's capture list per the exact capture
      lists specified in design.md's pseudocode (steps 6d-6f, 7, 8)
    - Add `mfaToken` to the `exchangeCodeForToken` success callback's capture list (reusing it
      directly as `users.id` — no new variable needed)
    - After `exchangeCodeForToken` succeeds, insert a new best-effort async
      `UPDATE users SET mfa_pending_client_id = NULL, mfa_pending_redirect_uri = NULL WHERE id
      = $1` (bound to `mfaToken`); both its success AND error callbacks build and send the
      identical successful response (`mfa_verified: true`, `message`, token fields) — a
      `LOG_ERROR` is emitted on the error path only, tokens are still returned either way
    - _Bug_Condition: isBugCondition(input) — unregistered client OR non-whitelisted redirect_uri OR mismatched pending binding_
    - _Expected_Behavior: Requirements 2.1, 2.2, 2.3, 2.4, 2.5 — reject with AUTH_INVALID_CREDENTIALS and no code/token issuance for bug-condition inputs; issue tokens and clear pending binding for the legitimate path_
    - _Preservation: Requirements 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9 — wrong-TOTP/missing-field/unknown-token rejections, OAuth2StandardController/DeviceAuthController/TokenService/RBAC untouched, and the successful response shape_
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 3.3, 3.4, 3.5, 3.9_

  - [ ] 4.3 Re-run exploratory tests from Task 2 — confirm bug is now fixed
    - **Property 1: Expected Behavior** - Cross-client auth confusion rejected
    - **IMPORTANT**: Re-run the SAME test file from Task 2 — do NOT write new tests
    - Run `Property1_MfaCrossClientAuthFix_ExploratoryTest.cc`'s 4 test cases against the now
      FIXED code
    - **EXPECTED OUTCOME**: Test Cases 1-3 now FAIL to issue tokens — each rejects with
      `AUTH_INVALID_CREDENTIALS` (401), confirming the bug is fixed. Test Case 4 (pending-binding
      absent) now also rejects (`NULL` treated as mismatch)
    - _Requirements: 2.1, 2.2, 2.3_

  - [ ] 4.4 Re-run preservation tests from Task 3 — confirm no regressions
    - **Property 2: Preservation** - Legitimate matching-binding path and unrelated rejections
    - **IMPORTANT**: Re-run the SAME test file from Task 3 — do NOT write new tests
    - Run `Property7_MfaCrossClientAuthFix_PreservationTest.cc` against the now FIXED code
    - **EXPECTED OUTCOME**: All property tests still PASS (matching-binding path still issues
      tokens with unchanged response shape; wrong-TOTP/missing-field/unknown-token/non-MFA-login
      behaviors all unchanged)
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.9_

### Phase 3: Validation

- [ ] 5. Fix-check property-based tests for Properties 1-3, 5, 6 (bug conditions rejected + new persist/clear behavior)
  - Create `OAuth2Server/test/integration/auth/Property1_MfaCrossClientAuthFix_UnregisteredClientRejected.cc`:
    PBT generating random unregistered `client_id` values; for all, assert `AUTH_INVALID_CREDENTIALS`
    (401) and no authorization code/token generated
  - Create `OAuth2Server/test/integration/auth/Property2_MfaCrossClientAuthFix_NonWhitelistedRedirectUriRejected.cc`:
    PBT generating random non-whitelisted redirect_uri values for a registered client; for all,
    assert `AUTH_INVALID_CREDENTIALS` (401) and no code/token generated
  - Create `OAuth2Server/test/integration/auth/Property3_MfaCrossClientAuthFix_MismatchedPendingBindingRejected.cc`:
    PBT generating random registered-client/whitelisted-redirect_uri combinations across multiple
    clients that do NOT match the recorded pending binding; for all, assert
    `AUTH_INVALID_CREDENTIALS` (401), no code/token generated, and pending columns unmodified
  - Create `OAuth2Server/test/integration/auth/Property5_MfaCrossClientAuthFix_PendingBindingClearedOnSuccess.cc`:
    for matching-binding requests, assert `mfa_pending_client_id`/`mfa_pending_redirect_uri` are
    `NULL` in the DB after a successful `verifyLogin`
  - Create `OAuth2Server/test/integration/auth/Property6_MfaCrossClientAuthFix_LoginPersistsPendingBinding.cc`:
    PBT generating random registered client_id/redirect_uri pairs on `SessionController::login`
    for an MFA-enabled user; for all, assert `mfa_pending_client_id`/`mfa_pending_redirect_uri`
    are persisted correctly before the `mfa_required` response is sent
  - Implement each pseudocode contract from design.md's "Fix Checking" section:
    ```
    FOR ALL input WHERE isBugCondition(input) DO
      result := verifyLogin_fixed(input)
      ASSERT result.errorCode = "AUTH_INVALID_CREDENTIALS"
      ASSERT result.httpStatus = 401
      ASSERT NOT authorizationCodeWasGenerated(input)
      ASSERT NOT tokensWereIssued(input)
    END FOR
    ```
  - Also include a positive PBT case for Property 4 (matching-binding still issues tokens) as a
    sanity companion within `Property5_*` or a dedicated
    `Property4_MfaCrossClientAuthFix_MatchingBindingPreservesTokenIssuance.cc`, generating random
    valid matching client/redirect_uri pairs across multiple registered clients and asserting the
    frozen response shape
  - Run all against the FIXED code from Task 4 — expect all assertions to PASS
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6_

- [ ] 6. Preservation/regression tests for Property 7 (non-MFA login and unrelated rejections unchanged)
  - Extend `Property7_MfaCrossClientAuthFix_PreservationTest.cc` (from Task 3) into randomized PBT
    coverage per design.md's pseudocode:
    ```
    FOR ALL input WHERE NOT isBugCondition(input) DO
      ASSERT verifyLogin_original(input) = verifyLogin_fixed(input)
    END FOR
    ```
  - Extend/reuse `OAuth2Server/test/integration/auth/LoginEnforcementTest.cc`-style coverage for:
    non-MFA login (no pending-column writes, unchanged authorization-code response),
    wrong-TOTP rejection, missing-field rejection (`mfa_token`/`code`/`client_id`/`redirect_uri`),
    and unknown-`mfa_token` rejection — confirm byte-for-byte-equivalent behavior to the unfixed
    baseline observed in Task 3
  - Run against the FIXED code from Task 4 — expect all PASS (no regressions)
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8_

- [ ] 7. Integration tests: end-to-end flows
  - Create `OAuth2Server/test/integration/auth/MfaCrossClientAuthFix_IntegrationTest.cc`
  - **Happy-path flow**: `SessionController::login` (MFA-enabled user, `vue-client`) →
    `MfaController::verifyLogin` with the same `client_id`/`redirect_uri` → tokens issued →
    confirm `mfa_pending_client_id`/`mfa_pending_redirect_uri` are `NULL` in the DB afterward
    (Properties 4, 5, 6)
  - **Cross-client rejection flow**: `SessionController::login` as `vue-client` →
    `MfaController::verifyLogin` with `admin-console`'s own valid registered
    `client_id`/`redirect_uri` → rejected with `AUTH_INVALID_CREDENTIALS` → confirm no new row
    was created in `oauth2_codes`/`oauth2_access_tokens` for that attempt (Property 3)
  - **Regression flow**: re-run existing `LoginEnforcementTest.cc` non-MFA-login and
    account-lockout flows to confirm the new DB write in `SessionController::login`'s
    `mfaEnabled` branch has no effect on any other branch (Property 7)
  - _Requirements: 2.3, 2.4, 2.5, 2.6, 3.1_

- [ ] 8. Checkpoint - full build and existing regression suite
  - Rebuild `OAuth2Test_test` and run the full suite (the existing ~228 tests plus all new tests
    from Tasks 2, 3, 5, 6, 7)
  - Confirm: all new fix-check tests (Task 5) PASS, all preservation tests (Tasks 3, 4.4, 6) PASS,
    all integration tests (Task 7) PASS, and zero pre-existing tests regress
  - If any pre-existing test fails, diagnose whether it's a genuine regression from Tasks 4.1/4.2
    before concluding the fix is complete — ask the user if the root cause is unclear
  - Confirm the migration (Task 1) applies cleanly on a fresh test database via `SchemaManager`
  - Ensure all tests pass; ask the user if questions arise
  - _Requirements: all (2.1-2.6, 3.1-3.9)_

## Notes

- All rejections introduced by this fix reuse the existing `AUTH_INVALID_CREDENTIALS` (401) error
  code — no new error codes are introduced, preserving the no-oracle property described in
  bugfix.md's Introduction.
- The `SessionController::login` write (Task 4.1) is fail-closed (`DB_QUERY_ERROR` on failure);
  the `MfaController::verifyLogin` clear-to-NULL write (Task 4.2, step 9) is best-effort (tokens
  are still returned even if the clear fails) — this asymmetry is intentional per design.md's
  rationale and must not be "fixed" to be symmetric.
- Out of scope (per bugfix.md and design.md, do not address in any task above): `mfa_token`
  format/lifetime/randomness, scope validation in `verifyLogin`, PKCE on the MFA second-factor
  leg, and `TokenService`/`consumeAuthCode`'s existing redirect_uri equality logic for the general
  authorization_code grant flow.
