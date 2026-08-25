# Implementation Plan: auth-flow-error-code-gaps

## Overview

This plan implements the 9 new `Error_Catalog` entries, routes 7 backend gaps (G1-G6, G7-as-regression) to precise error codes, syncs the two frontend `zh-CN.ts` localization catalogs, and adds the property-based and regression tests defined in the design's Testing Strategy. Work proceeds catalog-first (so downstream controller changes have valid codes to reference), then gap-by-gap through the backend controllers/services, then the frontend catalogs, then the Hodor integration (isolated as its own batch per the design's risk note), with checkpoints between major groups.

## Tasks

- [ ] 1. Add 9 new entries to Error_Catalog
  - [ ] 1.1 Append new RawEntry rows to ErrorCatalog
    - Append the 9 `RawEntry` rows (`VALIDATION_USERNAME_TAKEN` 3006, `VALIDATION_EMAIL_TAKEN` 3007, `VALIDATION_CREDENTIAL_ALREADY_REGISTERED` 3008, `VALIDATION_RESET_TOKEN_INVALID` 3009, `VALIDATION_VERIFICATION_TOKEN_INVALID` 3010, `VALIDATION_DEVICE_CODE_INVALID` 3011, `VALIDATION_RATE_LIMITED` 3012, `AUTH_MFA_CODE_INVALID` 4004, `AUTH_MFA_NOT_CONFIGURED` 4005) to `rawEntries()` in `OAuth2Plugin/src/error/ErrorCatalog.cc`, resizing the backing `std::array<RawEntry, N>` from 16 to 25
    - Do not modify, reorder, or renumber any existing entries
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5, 8.6_

- [ ] 2. Checkpoint - Ensure catalog tests pass
  - Run the existing `ErrorCatalogPropertyTest.cc` and `ErrorCatalogDocTest.cc` (inherited Property 5 coverage) to confirm the 9 new entries satisfy segment range, uniqueness, and field-completeness rules automatically. Ensure all tests pass, ask the user if questions arise.

- [ ] 3. Implement G1: registration duplicate username/email routing
  - [ ] 3.1 Change `AuthService::RegisterCallback` signature and insert-failure routing
    - Update the callback typedef in `AuthService.h` from free-text error message to structured `errorCode` string (empty = success)
    - In `AuthService.cc::registerUser`, in the `DrogonDbException` catch branch, match `e.base().what()` against `users_username_key` and `idx_users_email_unique` substrings to invoke the callback with `VALIDATION_USERNAME_TAKEN`, `VALIDATION_EMAIL_TAKEN`, or the existing fallback `VALIDATION_INVALID_INPUT`, with username-conflict checked first
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5_
  - [ ] 3.2 Update SessionController registration callback to forward the code
    - In `SessionController.cc::registerUser`'s failure callback (~line 846), replace the hardcoded `respondError(req, callback, "VALIDATION_INVALID_INPUT", ...)` with direct forwarding of the received `errorCode` string to `Error_Responder`, with no text inspection or hardcoded fallback; treat an empty string as success and skip calling `Error_Responder`
    - _Requirements: 1.6, 1.7_
  - [ ]* 3.3 Write property test for registration failure routing
    - **Property 1: 注册失败原因到 Error_Code 的精确路由**
    - **Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7**
  - [ ]* 3.4 Write regression tests for registration duplicate username/email
    - Duplicate username → HTTP 409 + `VALIDATION_USERNAME_TAKEN`
    - Duplicate email → HTTP 409 + `VALIDATION_EMAIL_TAKEN`
    - _Requirements: 1.1, 1.2_

- [ ] 4. Implement G2: MFA verification failure routing
  - [ ] 4.1 Route MfaController verifySetup failure branches to precise codes
    - In `MfaController.cc::verifySetup`, change the "not yet configured" branch (~line 150) to use `AUTH_MFA_NOT_CONFIGURED` and the TOTP-mismatch branch (~line 164) to use `AUTH_MFA_CODE_INVALID`, changing only the `code` argument passed to `respondError`
    - _Requirements: 2.1, 2.2_
  - [ ]* 4.2 Write property test for MFA verification routing
    - **Property 2: MFA 校验失败原因到 Error_Code 的精确路由**
    - **Validates: Requirements 2.1, 2.2**
  - [ ]* 4.3 Write regression tests for MFA verification failures
    - MFA code incorrect → `AUTH_MFA_CODE_INVALID`
    - MFA not configured → `AUTH_MFA_NOT_CONFIGURED`
    - _Requirements: 2.1, 2.2_

- [ ] 5. Implement G3: WebAuthn duplicate credential routing
  - [ ] 5.1 Detect credential_id conflict in WebAuthnController registerFinish
    - In `WebAuthnController.cc::registerFinish` (~line 215), in the insert's `DrogonDbException` handler, match `e.base().what()` for `webauthn_credentials` and `credential_id` substrings; on match respond with `VALIDATION_CREDENTIAL_ALREADY_REGISTERED` (HTTP 409) and return before falling through; otherwise keep the existing `DB_QUERY_ERROR` fallback
    - Ensure the existing credential record is left unmodified when a conflict is detected (no update/overwrite path is taken)
    - _Requirements: 3.1, 3.2, 3.3_
  - [ ]* 5.2 Write property test for WebAuthn registration failure routing
    - **Property 3: WebAuthn 凭据注册失败原因到 Error_Code 的精确路由**
    - **Validates: Requirements 3.1, 3.2, 3.3**
  - [ ]* 5.3 Write regression test for WebAuthn duplicate credential
    - Duplicate credential_id registration → HTTP 409 + `VALIDATION_CREDENTIAL_ALREADY_REGISTERED`
    - _Requirements: 3.1_

- [ ] 6. Implement G4: device authorization code invalidity routing
  - [ ] 6.1 Route DeviceAuthController approveDevice not-affected branch
    - In `DeviceAuthController.cc::approveDevice` (~line 259), change the `result.affectedRows() == 0` branch's code from `VALIDATION_INVALID_INPUT` to `VALIDATION_DEVICE_CODE_INVALID`, keeping the single-code behavior for "not found"/"already processed"/"expired" and leaving the device authorization record's state unchanged
    - _Requirements: 4.1, 4.2, 4.3_
  - [ ]* 6.2 Write property test for device code indistinguishability
    - **Property 4: 设备授权码失效原因的不可区分性**
    - **Validates: Requirements 4.1, 4.2, 4.3**
  - [ ]* 6.3 Write regression test for device code reuse/expiry
    - Reused or expired device code → `VALIDATION_DEVICE_CODE_INVALID`
    - _Requirements: 4.1_

- [ ] 7. Implement G5: password reset and email verification token invalidity routing
  - [ ] 7.1 Route PasswordResetController confirm token-invalid branch
    - In `PasswordResetController.cc::confirm`, change the invalid-token branch's code to `VALIDATION_RESET_TOKEN_INVALID`, preserving the existing anti-enumeration behavior of not distinguishing "not found"/"malformed"/"expired"/"used"
    - _Requirements: 5.1, 5.3_
  - [ ] 7.2 Route EmailVerificationController verify token-invalid branch
    - In `EmailVerificationController.cc::verify`, change the invalid-token branch's code to `VALIDATION_VERIFICATION_TOKEN_INVALID`, preserving the same anti-enumeration behavior
    - _Requirements: 5.2, 5.4_
  - [ ]* 7.3 Write property test for password reset token indistinguishability
    - **Property 5: 密码重置 token 失效原因的不可区分性**
    - **Validates: Requirements 5.1, 5.3**
  - [ ]* 7.4 Write property test for email verification token indistinguishability
    - **Property 6: 邮箱验证 token 失效原因的不可区分性**
    - **Validates: Requirements 5.2, 5.4**
  - [ ]* 7.5 Write regression tests for reset/verification token invalidity
    - Invalid/expired/used reset token → `VALIDATION_RESET_TOKEN_INVALID`
    - Invalid/expired/used verification token → `VALIDATION_VERIFICATION_TOKEN_INVALID`
    - _Requirements: 5.1, 5.2_

- [ ] 8. Add G7 regression test for account lockout anti-enumeration (no code change)
  - [ ]* 8.1 Write regression test asserting lockout and wrong-password responses are identical
    - Explicitly assert that a login attempt during account lockout and a login attempt with a wrong password both return `AUTH_INVALID_CREDENTIALS` with identical HTTP status and response body structure (excluding Request_ID), fixing this invariant against future accidental "fixes"
    - _Requirements: 7.1, 7.2_
  - [ ]* 8.2 Write property test for account lockout / wrong password indistinguishability
    - **Property 7: 账户锁定与密码错误的防枚举不可区分性**
    - **Validates: Requirements 7.1, 7.2**

- [ ] 9. Checkpoint - Ensure all backend controller/service tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 10. Sync frontend localization catalogs
  - [ ] 10.1 Add new error code entries to OAuth2Frontend zh-CN catalog
    - Append the 9 key-value pairs (`VALIDATION_USERNAME_TAKEN`, `VALIDATION_EMAIL_TAKEN`, `VALIDATION_CREDENTIAL_ALREADY_REGISTERED`, `VALIDATION_RESET_TOKEN_INVALID`, `VALIDATION_VERIFICATION_TOKEN_INVALID`, `VALIDATION_DEVICE_CODE_INVALID`, `VALIDATION_RATE_LIMITED`, `AUTH_MFA_CODE_INVALID`, `AUTH_MFA_NOT_CONFIGURED`) to `OAuth2Frontend/src/services/messages/zh-CN.ts` using the exact wording specified in the design's Frontend Design section
    - _Requirements: 9.1, 9.4_
  - [ ] 10.2 Add identical entries to OAuth2Admin zh-CN catalog
    - Append the same 9 key-value pairs, character-for-character identical to the OAuth2Frontend entries, to `OAuth2Admin/src/services/messages/zh-CN.ts`
    - _Requirements: 9.2, 9.3, 9.4_
  - [ ]* 10.3 Verify existing frontend property tests cover the new entries
    - Run the existing `messageCatalog.property.test.ts` (inherited Property 13/14 coverage) to confirm it passes with the new entries with no test code changes needed
    - _Requirements: 9.1, 9.2, 9.3_

- [ ] 11. Checkpoint - Ensure frontend catalog tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 12. Implement G6: Hodor rate-limit rejection Envelope wiring
  - [ ] 12.1 Wire Hodor rejection response to Error_Envelope format
    - In `OAuth2Server/main.cc`, after the existing Hodor plugin load-status check, if `drogon::app().getPlugin<drogon::plugin::Hodor>()` returns non-null, call its reject-response customization hook to build the response via `common::error::Error::fromCode("VALIDATION_RATE_LIMITED", requestId)` and `common::error::ErrorResponder::buildResponse`, replacing Hodor's plain-text rejection body
    - Verify the actual `drogon::plugin::Hodor` header for the correct hook name/signature before wiring; if no equivalent hook exists, implement the rejection Envelope in a custom filter layer instead
    - Guard the wiring so it only executes when the plugin is confirmed loaded; skip silently otherwise so startup is never blocked or aborted by plugin absence or indeterminate load state
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5, 6.6, 6.7_
  - [ ]* 12.2 Write regression test for rate-limit rejection Envelope
    - Triggered rate-limited request → JSON Error Envelope body, `VALIDATION_RATE_LIMITED` code, HTTP 429
    - _Requirements: 6.1, 6.2, 6.3_

- [ ] 13. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP; they are not implemented by the coding agent by default.
- Task 1 must complete before any gap-routing task (3-8) since those tasks reference the new Error_Codes.
- Task 12 (G6) is deliberately sequenced after all controller-level gaps and isolated as its own group, per the design's note that global plugin wiring carries higher risk and should ship as an independent batch.
- Requirement 8 and 9's data-shape/coverage properties (inherited Property 5, 13, 14 from `error-code-message-standardization`) are validated automatically by existing test suites once the new catalog entries and localization keys are added; no new property test code is needed for them (see tasks 2 and 10.3).
- Each task references specific requirements for traceability.

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1"] },
    { "id": 1, "tasks": ["3.1", "4.1", "5.1", "6.1", "7.1", "7.2", "8.1", "8.2", "10.1", "10.2"] },
    { "id": 2, "tasks": ["3.2", "3.3", "3.4", "4.2", "4.3", "5.2", "5.3", "6.2", "6.3", "7.3", "7.4", "7.5", "10.3", "12.1"] },
    { "id": 3, "tasks": ["12.2"] }
  ]
}
```
