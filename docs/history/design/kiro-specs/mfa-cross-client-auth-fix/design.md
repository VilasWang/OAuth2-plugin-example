# MFA Cross-Client Auth Fix Bugfix Design

## Overview

PR #9's `MfaController::verifyLogin` issues real OAuth2 tokens after a successful TOTP check, but
it never verifies that the `client_id`/`redirect_uri` supplied on the *second-factor* request are
(a) a registered client, (b) a whitelisted redirect_uri for that client, and (c) the *same*
client/redirect_uri pair that was used on the *first-factor* `SessionController::login` call that
produced the `mfa_token`. Because `mfa_token` is nothing more than `std::to_string(internalId)`
with no client binding, an attacker holding a valid `mfa_token` + correct TOTP code can substitute
any other registered client's `client_id`/`redirect_uri` and receive a token minted for that other
client (P0-1), and `redirect_uri` is never checked against a whitelist at all inside this call path
(P0-2).

The fix adds a **login-session binding**: two new nullable columns on `users`
(`mfa_pending_client_id`, `mfa_pending_redirect_uri`) that `SessionController::login` populates
when it returns `mfa_required`, and that `MfaController::verifyLogin` reads back and compares
against the request's `client_id`/`redirect_uri` before doing anything else. `verifyLogin` also
gains the standard `validateClient`/`validateRedirectUri` registration + whitelist checks already
used by `OAuth2StandardController::authorize`. All new rejections reuse the existing
`AUTH_INVALID_CREDENTIALS` (401) error code so a wrong TOTP code, an unknown client, a
non-whitelisted redirect_uri, and a binding mismatch are all indistinguishable from the outside (no
oracle for client registration). On a fully successful verification, the pending columns are
cleared to `NULL` so the binding cannot be replayed by an unrelated later verification attempt.

## Glossary

- **Bug_Condition (C)**: The condition under which `verifyLogin` currently issues a token it should
  reject — i.e. any request where the client is unregistered, the redirect_uri is not whitelisted
  for that client, or the client_id/redirect_uri pair does not match the pending first-factor login
  session — while the TOTP code and `mfa_token` are otherwise valid.
- **Property (P)**: The fixed function must reject all such requests with `AUTH_INVALID_CREDENTIALS`
  (401) and must not generate an authorization code or issue tokens.
- **Preservation**: The unchanged behaviors that must survive the fix unmodified — legitimate
  same-client verification, TOTP-incorrect rejection, missing-field rejection, unknown-`mfa_token`
  rejection, and all behavior of `SessionController::login`/`TokenService`/`consumeAuthCode`/scope
  validation/PKCE outside the `verifyLogin` binding check.
- **mfa_token**: `std::to_string(internalId)`, the second-factor session identifier returned by
  `SessionController::login` when `authResult->mfaEnabled` is true. Unchanged by this fix (format,
  lifetime, randomness — see PRD-accepted limitation on concurrent-login overwrite).
- **mfa_pending_client_id / mfa_pending_redirect_uri**: New nullable `users` columns. Populated by
  `SessionController::login` at the moment it returns `mfa_required`; read and compared by
  `MfaController::verifyLogin`; cleared to `NULL` by `verifyLogin` after tokens are successfully
  issued.
- **Pending binding**: The `(mfa_pending_client_id, mfa_pending_redirect_uri)` pair stored for a
  given `users.id`, representing "the client/redirect_uri the first-factor login was performed
  for."
- **validateClient / validateRedirectUri**: Existing async `OAuth2Plugin` methods
  (`OAuth2Plugin.h:64-77`) that delegate to `ClientService`. `validateClient(clientId, "", cb)`
  confirms the client is registered (empty secret is correct for PUBLIC clients).
  `validateRedirectUri(clientId, redirectUri, cb)` confirms `redirectUri` is in that client's
  registered whitelist. Neither is modified by this fix; they are newly *invoked* from
  `verifyLogin`.
- **sharedCb**: The existing `MfaController.cc` pattern —
  `std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback))` — used to
  keep one response callback alive and copyable across nested async lambdas. This fix follows this
  exact pattern; it does **not** introduce `enable_shared_from_this` into `MfaController`.

## Bug Details

### Bug Condition

The bug manifests whenever `verifyLogin` is invoked with a valid `mfa_token` (resolves to an
existing `users.id`) and a correct TOTP `code`, but the request's `client_id`/`redirect_uri` fail
one of three checks that the unfixed code never performs: client registration, redirect_uri
whitelist membership, or equality with the pending first-factor login-session binding.

**Formal Specification:**
```
FUNCTION isBugCondition(input)
  INPUT: input of type VerifyLoginRequest
    { mfaToken, code, clientId, redirectUri, scope, nonce }
  OUTPUT: boolean

  LET user := lookupUserByInternalId(input.mfaToken)   // SELECT ... WHERE id = mfaToken

  RETURN user EXISTS
         AND totpValid(user.mfa_secret, input.code) = true
         AND (
               NOT isRegisteredClient(input.clientId)
               OR NOT isWhitelistedRedirectUri(input.clientId, input.redirectUri)
               OR (input.clientId, input.redirectUri)
                    != (user.mfa_pending_client_id, user.mfa_pending_redirect_uri)
             )
         AND unfixedVerifyLogin(input) generates an authorization code and issues tokens
             (i.e. F does NOT reject)
END FUNCTION
```

### Examples

- **Cross-client confusion (1.3)**: User A logs in via `vue-client` /
  `http://localhost:5173/callback`, gets `mfa_required` + `mfa_token=42`. Attacker (who also
  obtained `mfa_token=42` and the correct TOTP code, e.g. via a compromised device) POSTs to
  `/oauth2/mfa/verify` with `client_id=admin-console`, `redirect_uri=http://localhost:5173/admin/callback`
  — both independently valid/registered/whitelisted for `admin-console`. Unfixed: token issued,
  bound to `admin-console`. Fixed: rejected with `AUTH_INVALID_CREDENTIALS` because
  `(admin-console, .../admin/callback) != (vue-client, .../callback)` (the recorded pending
  binding).
- **Unregistered client (1.1)**: Same valid `mfa_token`/`code`, but `client_id=not-a-real-client`.
  Unfixed: token issued anyway. Fixed: rejected before any DB code/token work happens.
- **Non-whitelisted redirect_uri (1.2)**: `client_id=vue-client` (registered) but
  `redirect_uri=https://evil.example/cb` (not in `vue-client`'s whitelist). Unfixed: token issued
  anyway, `consumeAuthCode`'s equality check passes trivially because both sides come from the same
  request body. Fixed: rejected.
- **Legitimate matching request (2.4, not the bug)**: `client_id=vue-client`,
  `redirect_uri=http://localhost:5173/callback`, matching the pending binding recorded at login
  time. Both fixed and unfixed issue tokens; this is the case the fix must continue to allow.

## Expected Behavior

### Preservation Requirements

**Unchanged Behaviors:**
- `SessionController::login` for users without MFA enabled: no pending-column writes, no new
  validation, identical authorization-code generation and response.
- `SessionController::login`'s `mfa_required` response shape and `mfa_token` format
  (`std::to_string(internalId)`) when MFA is enabled.
- `verifyLogin` rejecting an incorrect TOTP code with `AUTH_INVALID_CREDENTIALS` ("verifyLogin: TOTP
  code is incorrect").
- `verifyLogin` rejecting an unknown `mfa_token` with `AUTH_INVALID_CREDENTIALS` ("verifyLogin:
  invalid MFA session").
- `verifyLogin` rejecting a request missing `mfa_token`, `code`, `client_id`, or `redirect_uri` with
  `VALIDATION_MISSING_REQUIRED_FIELD`.
- `OAuth2StandardController::authorize` and `DeviceAuthController::deviceAuthorization`'s existing
  validation chains (`ClientService::validateClient`/`validateRedirectUri` themselves are reused,
  not modified).
- `TokenService::exchangeCodeForToken`/`consumeAuthCode`'s redirect_uri equality check for the
  general authorization_code grant flow, outside the `verifyLogin` internal call path.
- `AuthorizationFilter`/RBAC reading roles from the database rather than token scope.
- The successful response shape on a fully valid request: `mfa_verified: true`, `message: "MFA
  verification successful"`, plus token fields — unchanged.

**Scope:**
All inputs that do NOT trigger one of the three new checks (unregistered client, non-whitelisted
redirect_uri, mismatched pending binding) should be completely unaffected by this fix. This
includes:
- Non-MFA logins through `SessionController::login`.
- `verifyLogin` requests with missing fields, wrong TOTP codes, or unknown `mfa_token`s.
- `verifyLogin` requests where `client_id`/`redirect_uri` are registered, whitelisted, AND match the
  pending binding (the legitimate, intended path).
- Any endpoint other than `SessionController::login` and `MfaController::verifyLogin`.

## Hypothesized Root Cause

1. **Missing client/redirect_uri validation in `verifyLogin`**: Unlike
   `OAuth2StandardController::authorize` (`OAuth2StandardController.cc:839,864`), which runs
   `validateClient` → `validateRedirectUri` → `validateClientScopes` before issuing anything,
   `verifyLogin` (`MfaController.cc:305-318`) only checks `clientId`/`redirectUri` are *non-empty*
   strings. It never calls `plugin->validateClient`/`plugin->validateRedirectUri` at all.

2. **No session binding between first-factor and second-factor requests**: `mfa_token`
   (`SessionController.cc:426`, `std::to_string(authResult->internalId)`) carries no client context.
   `verifyLogin` has no way today to know which `client_id`/`redirect_uri` the original `login` call
   used, so it trusts whatever the second request claims.

3. **`consumeAuthCode`'s redirect_uri check is self-referential in this path**: Because
   `generateAuthorizationCode` is called with the *same* `redirectUri` that
   `exchangeCodeForToken`/`consumeAuthCode` later compares against, the equality check inside
   `TokenService` always trivially passes here — it was never designed to be the whitelist gate,
   that responsibility sits with `validateRedirectUri` which is simply never invoked on this leg.

4. **Duplicate plugin lookup masking the missing top-level check**: The existing `getPlugin` call
   is buried inside the TOTP-success branch (`MfaController.cc:350-357`), so there was no natural
   single "gate" point near the top of the function where a reviewer would expect to see
   client/redirect_uri validation, making the gap easy to miss during PR #9's review.

## Correctness Properties

Property 1: Bug Condition - Unregistered client rejected

_For any_ `verifyLogin` request with a valid `mfa_token`, a correct TOTP `code`, and a `client_id`
that is NOT a registered client (`isBugCondition` holds via the registration clause), the fixed
function SHALL reject with `AUTH_INVALID_CREDENTIALS` (HTTP 401) and SHALL NOT call
`generateAuthorizationCode` or `exchangeCodeForToken`.

**Validates: Requirements 2.1**

Property 2: Bug Condition - Non-whitelisted redirect_uri rejected

_For any_ `verifyLogin` request with a valid `mfa_token`, a correct TOTP `code`, a registered
`client_id`, and a `redirect_uri` that is NOT in that client's registered whitelist, the fixed
function SHALL reject with `AUTH_INVALID_CREDENTIALS` (HTTP 401) and SHALL NOT call
`generateAuthorizationCode` or `exchangeCodeForToken`.

**Validates: Requirements 2.2**

Property 3: Bug Condition - Cross-client / mismatched pending-binding rejected

_For any_ `verifyLogin` request with a valid `mfa_token`, a correct TOTP `code`, a registered
`client_id`, and a whitelisted `redirect_uri` for that client, where `(client_id, redirect_uri)`
does NOT equal the `(mfa_pending_client_id, mfa_pending_redirect_uri)` recorded for that user, the
fixed function SHALL reject with `AUTH_INVALID_CREDENTIALS` (HTTP 401), SHALL NOT call
`generateAuthorizationCode` or `exchangeCodeForToken`, and SHALL NOT modify
`mfa_pending_client_id`/`mfa_pending_redirect_uri`.

**Validates: Requirements 2.3**

Property 4: Preservation - Matching binding still issues tokens

_For any_ `verifyLogin` request with a valid `mfa_token`, a correct TOTP `code`, a registered
`client_id`, a whitelisted `redirect_uri` for that client, and `(client_id, redirect_uri)` equal to
the recorded pending binding, the fixed function SHALL produce the same result as the original
function: generate an authorization code, exchange it for tokens, and return them bound to that
client with the existing response shape (`mfa_verified: true`, `message`, token fields).

**Validates: Requirements 2.4, 3.9**

Property 5: Bug Condition - Pending binding cleared after success

_For any_ `verifyLogin` request that completes successfully per Property 4, the fixed function
SHALL clear `mfa_pending_client_id` and `mfa_pending_redirect_uri` for that user back to `NULL`
after tokens are issued, so a later, unrelated verification attempt has no binding left to reuse.

**Validates: Requirements 2.5**

Property 6: Bug Condition - Login persists pending binding when MFA required

_For any_ `SessionController::login` call where `authResult->mfaEnabled` is true, the fixed function
SHALL persist that request's `client_id` and `redirect_uri` into
`mfa_pending_client_id`/`mfa_pending_redirect_uri` for `authResult->internalId` before the
`mfa_required` JSON response is sent to the client.

**Validates: Requirements 2.6**

Property 7: Preservation - Non-MFA login and unrelated verifyLogin rejections unchanged

_For any_ input where none of the bug conditions in Properties 1-3 hold — non-MFA logins through
`SessionController::login`, `verifyLogin` requests with missing fields, wrong TOTP codes, or unknown
`mfa_token`s — the fixed code SHALL produce exactly the same behavior (response body, status code,
DB side effects) as the original code.

**Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8**

## Fix Implementation

### Changes Required

#### 1. Schema migration

**File**: `OAuth2Server/sql/migrations/V022__mfa_pending_client_binding.sql` (next number after
`V021__widen_email_verification_tokens_email.sql`)

Follows the exact idempotent `ALTER TABLE ... ADD COLUMN IF NOT EXISTS` convention used by
`V011__mfa_support.sql` (`mfa_secret VARCHAR(64)`) and `V013__account_lockout.sql`. Column widths
mirror existing conventions: `oauth2_clients.client_id` is `VARCHAR(50)` (`V002__oauth2_core.sql`),
so `mfa_pending_client_id` matches that width; `redirect_uri` is stored as `TEXT` elsewhere
(`oauth2_clients.redirect_uris`, `oauth2_codes.redirect_uri` in `V002__oauth2_core.sql`), so
`mfa_pending_redirect_uri` is `TEXT`. No `DOWN`/rollback section is used, consistent with all
existing migrations in this project (none of `V001`-`V021` contain one) — migrations are
forward-only and applied in order by `SchemaManager`.

```sql
-- V022: MFA pending client/redirect_uri binding
-- Records the client_id/redirect_uri used on the first-factor login that
-- triggered mfa_required, so verifyLogin can reject a second-factor request
-- that supplies a different (even if independently valid) client/redirect_uri
-- pair. Fixes cross-client authorization confusion (P0-1).

ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_pending_client_id VARCHAR(50);
ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_pending_redirect_uri TEXT;
```

Both columns are nullable with no default (`NULL` by default), matching `mfa_secret`'s pattern —
"unset" is the natural resting state, and `NULL` is exactly the value `verifyLogin` writes back on
successful completion (Property 5), so no separate empty-string sentinel is introduced.

#### 2. `SessionController::login` — persist the pending binding

**File**: `OAuth2Server/controllers/SessionController.cc`

**Function**: `SessionController::login`, inside the `authResult->mfaEnabled` branch currently at
lines 420-431 (`=== CHECK 2: MFA enforcement ===`).

**Design decision — new DB call placement**: The current branch is synchronous (builds
`mfaResp` and calls `callback(resp)` immediately). This fix inserts one new async
`db->execSqlAsync` UPDATE *before* that response is built/sent, so the pending binding is durably
recorded before the client can possibly receive `mfa_token` and start a second-factor request.

**Design decision — DB error handling**: If the UPDATE fails, the fix responds with
`DB_QUERY_ERROR` and does **not** fall back to sending `mfa_required` anyway. Rationale: if the
pending binding cannot be persisted, `verifyLogin` would have no binding to compare against for
this login attempt. Two options were considered:
  - (a) Silently continue and return `mfa_required` regardless of the UPDATE outcome — rejected
    because it would leave `mfa_pending_client_id`/`mfa_pending_redirect_uri` in a stale state
    (possibly still holding a *previous* login's binding, since `mfa_token` has no incorporated
    nonce — see the accepted concurrent-login limitation), which the attacker could exploit exactly
    as the P0-1 bug this fix addresses if the persisted values are wrong for the current attempt.
  - (b) **Chosen**: Surface `DB_QUERY_ERROR` and abort the login attempt. This fails closed: the
    user must retry, but no `mfa_token` is ever issued against a not-yet-durably-recorded binding.
    This matches the project's existing convention of surfacing `DB_QUERY_ERROR` on write failures
    elsewhere (e.g. `MfaController::setup`, `MfaController::disable`) rather than best-effort
    degrading.

**Exact lambda nesting** (mirrors the existing `callback = std::move(callback)` capture-by-move
pattern used throughout this file, e.g. the `generateAuthorizationCode` call at
`SessionController.cc:472-497`):

```cpp
// === CHECK 2: MFA enforcement ===
if (authResult->mfaEnabled)
{
    // Persist this login attempt's client_id/redirect_uri as the pending
    // binding verifyLogin must match against (Requirement 2.6 / P0-1 fix).
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
      "UPDATE users SET mfa_pending_client_id = $1, mfa_pending_redirect_uri = $2 "
      "WHERE id = $3",
      req, internalId = authResult->internalId, callback = std::move(callback) mutable {
          // Only NOW build and send the mfa_required response - the pending
          // binding is durably recorded before the client can act on mfa_token.
          Json::Value mfaResp;
          mfaResp["mfa_required"] = true;
          mfaResp["mfa_token"] = std::to_string(internalId);
          mfaResp["message"] =
            "MFA verification required. Submit TOTP code to /oauth2/mfa/verify";
          auto resp = HttpResponse::newHttpJsonResponse(mfaResp);
          resp->setStatusCode(k200OK);
          callback(resp);
      },
      req, callback = std::move(callback) mutable {
          // Fail closed (see design rationale): do not return mfa_required if
          // the pending binding could not be persisted.
          respondError(
            req,
            std::move(callback),
            "DB_QUERY_ERROR",
            std::string("login: failed to persist MFA pending binding: ") + e.base().what()
          );
      },
      clientId,
      redirectUri,
      authResult->internalId
    );
    return;
}
```

Notes on capture correctness (house style: no `enable_shared_from_this` needed here —
`SessionController::login` has no member state, only local/lambda-captured values):
- `callback` is captured by move into the success lambda; the **error** lambda needs its own copy
  of `callback`, so `std::move(callback)` cannot be used in both branches of the same
  `execSqlAsync` call — exactly one of the two callbacks will ever fire, so `db->execSqlAsync`'s two
  callback parameters must each own their own copy/move of `callback`. Since `std::function` is
  only movable-into-one-place, the implementation captures `callback` **by move into the success
  lambda** (the far more common path in practice) and captures it **by copy into the error lambda**
  is not possible for a move-only-semantics `std::function<void(const HttpResponsePtr&)>` — in
  practice this codebase's existing pattern (see `MfaController::setup`,
  `MfaController.cc:89-107`) captures the *shared* callback (`sharedCb`) in both branches instead of
  moving a bare `std::function` twice. `SessionController::login`'s outer lambda already owns
  `callback` by move (from the outer `AuthService::validateUser` capture, `SessionController.cc:341`
  `callback = std::move(callback)`), so this fix converts it to a
  `std::make_shared<std::function<void(const HttpResponsePtr&)>>` at the top of the
  `mfaEnabled` branch (identical to `MfaController`'s `sharedCb` idiom) and captures that
  `shared_ptr` by value in both the success and error callbacks below, instead of attempting to
  move a bare `std::function` into two places. This is a **local** idiom change confined to the
  `mfaEnabled` branch; it does not affect any other branch of `login` (PKCE / non-MFA paths keep
  using the moved bare `callback` exactly as today, since only one branch of the outer `if`/`else`
  chain ever executes).
- `authResult->internalId` is copied into `internalId` (an `int`, cheap to copy) rather than
  capturing `authResult` (an `std::optional<AuthResult>` owned by the outer lambda) by reference,
  avoiding any lifetime dependency on the outer lambda's captured `authResult` staying alive across
  the async hop.
- `clientId`/`redirectUri` are already-copied local `std::string`s in `login`'s scope (parsed at the
  top of the function), consistent with how they are already captured elsewhere in this function
  (e.g. the `generateAuthorizationCode` call captures `redirectUri` at
  `SessionController.cc:472-479`).

#### 3. `MfaController::verifyLogin` — full restructuring

**File**: `OAuth2Server/controllers/MfaController.cc`

**Function**: `MfaController::verifyLogin` (currently `MfaController.cc:255-413`)

**New control flow** (pseudocode with exact lambda capture lists, following the existing
`sharedCb`/`shared_ptr<std::function<...>>` pattern already used throughout this file — no
`enable_shared_from_this` is introduced):

```
1. Parse mfaToken/code/clientId/redirectUri/scope/nonce                    [UNCHANGED, L260-291]
2. Non-empty checks on mfaToken/code, then clientId/redirectUri            [UNCHANGED, L294-318]
3. scope defaults to "openid profile email" if empty                      [UNCHANGED, L316-319]
4. sharedCb = make_shared<function<void(HttpResponsePtr)>>(move(callback)) [UNCHANGED pattern]
5. plugin = drogon::app().getPlugin<OAuth2Plugin>()                        [MOVED UP from L350]
     if (!plugin) -> respondError(INTERNAL_ERROR, "verifyLogin: OAuth2 Plugin not loaded"); return
6. db->execSqlAsync(
     "SELECT id, public_sub, mfa_secret, mfa_backup_codes,
             mfa_pending_client_id, mfa_pending_redirect_uri
      FROM users WHERE id = $1",                                          [EXTENDED SELECT]
     success cb, error cb, mfaToken
   )
     capture list (success cb): [sharedCb, code, mfaToken, req, clientId, redirectUri,
                                  scope, nonce, plugin]
       -- `plugin` added to the capture list since step 5 moved it above this call;
          everything else is unchanged from today's capture list (MfaController.cc:330).
     capture list (error cb):   [sharedCb, req]                            [UNCHANGED]

     ON success(r):
       6a. if r.empty() -> respondError(AUTH_INVALID_CREDENTIALS,
                                         "verifyLogin: invalid MFA session"); return  [UNCHANGED]
       6b. secret     := r[0]["mfa_secret"] or ""                                     [UNCHANGED]
           publicSub  := r[0]["public_sub"]                                           [UNCHANGED]
           pendingClientId  := r[0]["mfa_pending_client_id"] or ""            [NEW]
           pendingRedirectUri := r[0]["mfa_pending_redirect_uri"] or ""       [NEW]

       6c. if NOT totpValid(secret, code):
             respondError(AUTH_INVALID_CREDENTIALS, "verifyLogin: TOTP code is incorrect"); return
                                                                                       [UNCHANGED,
                                                                                        moved before
                                                                                        the new
                                                                                        checks below
                                                                                        - see note]

       6d. [NEW] plugin->validateClient(clientId, "", cb):
             capture list: [sharedCb, req, plugin, clientId, redirectUri, publicSub,
                             pendingClientId, pendingRedirectUri, scope, nonce]
             ON false -> respondError(AUTH_INVALID_CREDENTIALS,
                            "verifyLogin: unknown or invalid client"); return
             ON true  -> continue to 6e

       6e. [NEW] plugin->validateRedirectUri(clientId, redirectUri, cb):
             capture list: [sharedCb, req, plugin, clientId, redirectUri, publicSub,
                             pendingClientId, pendingRedirectUri, scope, nonce]
             ON false -> respondError(AUTH_INVALID_CREDENTIALS,
                            "verifyLogin: redirect_uri not registered for client"); return
             ON true  -> continue to 6f

       6f. [NEW] if (clientId, redirectUri) != (pendingClientId, pendingRedirectUri):
             respondError(AUTH_INVALID_CREDENTIALS,
                "verifyLogin: client/redirect_uri does not match login session"); return
           else continue to 7

       7. plugin->generateAuthorizationCode(clientId, publicSub, scope, redirectUri,
                                             "", "", nonce, cb)                    [UNCHANGED call,
                                                                                     now reached only
                                                                                     after 6d-6f]
            capture list: [sharedCb, req, plugin, clientId, redirectUri, publicSub] [UNCHANGED]
            ON !success -> respondError(INTERNAL_ERROR,
                              "verifyLogin: failed to generate authorization code: " + genError)
                            return
            ON success(authCode):
       8.     plugin->exchangeCodeForToken(authCode, clientId, "", redirectUri, "", cb)
                                                                                     [UNCHANGED call]
                capture list: [sharedCb, req, publicSub, mfaToken]         [`mfaToken` ADDED to the
                                                                             capture list - it is
                                                                             already the users.id
                                                                             (the outer SELECT is
                                                                             "WHERE id = $1" bound to
                                                                             mfaToken), so it is
                                                                             reused directly as the
                                                                             $1 parameter for the new
                                                                             clear-pending UPDATE
                                                                             below, with no separate
                                                                             "internalUserId"
                                                                             variable needed]
                ON tokenResult.isMember("error") -> respondError(INTERNAL_ERROR, ...); return
                                                                                     [UNCHANGED]
                ON success:
       9.         [NEW] db->execSqlAsync(
                     "UPDATE users SET mfa_pending_client_id = NULL,
                                        mfa_pending_redirect_uri = NULL
                      WHERE id = $1",
                     success cb, error cb, mfaToken
                   )
                     capture list (success cb): [sharedCb, req, publicSub, tokenResult]
                     capture list (error cb):   [sharedCb, req, publicSub, tokenResult]
                     -- both branches respond identically (see design decision below);
                        `tokenResult` is copied (Json::Value, cheap-ish, already a
                        local by-value parameter in the exchangeCodeForToken callback)
                        so the response can still be built if the clear fails.

                     ON EITHER outcome:
                       AuditLogger::log("mfa_verified", "success", req, publicSub,
                                         "user", publicSub)                        [UNCHANGED]
                       json := tokenResult; json["message"] = "MFA verification successful";
                       json["mfa_verified"] = true                                 [UNCHANGED]
                       resp := HttpResponse::newHttpJsonResponse(json)
                       (*sharedCb)(resp)                                           [UNCHANGED]
10. error cb (outer SELECT) -> respondError(DB_QUERY_ERROR,
       "MFA login verify failed: " + e.base().what())                              [UNCHANGED,
                                                                                      L406-413]
```

**Note on TOTP-check ordering (6c before 6d-6f)**: TOTP validity is checked *before* the new
client/redirect_uri checks, preserving the exact current ordering (today TOTP is checked
immediately after the SELECT, `MfaController.cc:343`). This means an incorrect TOTP code with an
also-invalid client still returns "verifyLogin: TOTP code is incorrect" rather than a client error
— consistent with Requirement 3.3 ("regardless of client/redirect_uri validity") and preserving the
existing no-oracle property (a wrong-TOTP request never reveals anything about client validity,
since it never reaches the new checks).

**Note on step 9's error handling (clearing the pending binding)**: If the `UPDATE ... SET
mfa_pending_client_id = NULL ...` fails, the response is still sent as MFA-verification-successful
(tokens have already been issued and cannot be un-issued). The failure is only that the pending
binding is not cleared — worst case, a later verification attempt with the *same* binding would
still be allowed to reuse it (not a new vulnerability: it's the same binding for the same user that
was already valid), so failing to clear it does not reopen P0-1/P0-2. This is deliberately treated
as best-effort cleanup, unlike the `SessionController::login` write in change #2 (which fails closed
because it is safety-critical — without it, `verifyLogin` would have no binding to check at all).
A `LOG_ERROR` is emitted on the error path for observability but the client still receives their
tokens.

**Duplicate `getPlugin` removal**: The `getPlugin<::OAuth2Plugin>()` call currently inside the TOTP
success branch (`MfaController.cc:350-357`) is deleted; `plugin` is instead obtained once at step 5
and threaded through every subsequent lambda's capture list.

## Testing Strategy

### Validation Approach

The testing strategy follows a two-phase approach: first, surface counterexamples that demonstrate
the bug on unfixed code, then verify the fix works correctly and preserves existing behavior.

### Exploratory Bug Condition Checking

**Goal**: Surface counterexamples that demonstrate the bug BEFORE implementing the fix. Confirm or
refute the root cause analysis. If we refute, we will need to re-hypothesize.

**Test Plan**: Seed a test database with two registered clients (e.g. `vue-client` /
`http://localhost:5173/callback` and `admin-console` / `http://localhost:5173/admin/callback`), a
user with MFA enabled, log in as that user via `SessionController::login` to obtain `mfa_token`,
then call `MfaController::verifyLogin` with mismatched/unregistered/non-whitelisted
`client_id`/`redirect_uri` combinations against the **unfixed** code and observe that tokens are
issued anyway.

**Test Cases**:
1. **Unregistered client test**: `verifyLogin` with a `client_id` not present in `oauth2_clients`
   (will succeed and issue tokens on unfixed code).
2. **Non-whitelisted redirect_uri test**: `verifyLogin` with a registered `client_id` but a
   `redirect_uri` not in its whitelist (will succeed on unfixed code).
3. **Cross-client confusion test**: log in as `vue-client`, then `verifyLogin` with
   `admin-console`'s own registered/whitelisted `client_id`/`redirect_uri` (will succeed and issue a
   token bound to `admin-console` on unfixed code).
4. **Pending-binding-absent edge case**: `verifyLogin` called when `mfa_pending_client_id` is still
   `NULL` (e.g. a user whose row predates this migration) — document expected fixed behavior
   (treated as a mismatch, i.e. rejected, since `NULL != any non-null clientId`).

**Expected Counterexamples**:
- Tokens are issued for requests that should be rejected.
- Root cause confirmed: `verifyLogin` never calls `validateClient`/`validateRedirectUri`, and has no
  binding to compare against at all prior to this fix.

### Fix Checking

**Goal**: Verify that for all inputs where the bug condition holds, the fixed function produces the
expected behavior.

**Pseudocode:**
```
FOR ALL input WHERE isBugCondition(input) DO
  result := verifyLogin_fixed(input)
  ASSERT result.errorCode = "AUTH_INVALID_CREDENTIALS"
  ASSERT result.httpStatus = 401
  ASSERT NOT authorizationCodeWasGenerated(input)
  ASSERT NOT tokensWereIssued(input)
END FOR
```

### Preservation Checking

**Goal**: Verify that for all inputs where the bug condition does NOT hold, the fixed function
produces the same result as the original function.

**Pseudocode:**
```
FOR ALL input WHERE NOT isBugCondition(input) DO
  ASSERT verifyLogin_original(input) = verifyLogin_fixed(input)
END FOR
```

**Testing Approach**: Property-based testing is recommended for preservation checking because:
- It generates many test cases automatically across the input domain (client/redirect_uri
  combinations, TOTP correctness, mfa_token validity).
- It catches edge cases that manual unit tests might miss (e.g. empty pending binding, whitespace
  differences in redirect_uri).
- It provides strong guarantees that the legitimate matching-binding path and all pre-existing
  rejection paths are unaffected.

**Test Plan**: Observe behavior on UNFIXED code first for the legitimate matching-binding path,
wrong-TOTP rejection, missing-field rejection, and unknown-`mfa_token` rejection, then write
property-based tests capturing that behavior and assert it is preserved after the fix.

**Test Cases**:
1. **Matching-binding preservation**: Observe that a correctly-matching `client_id`/`redirect_uri`
   request issues tokens with the existing response shape on unfixed code, then verify this
   continues after the fix (Property 4).
2. **Wrong-TOTP preservation**: Observe `AUTH_INVALID_CREDENTIALS` ("TOTP code is incorrect") on
   unfixed code regardless of client/redirect_uri validity, then verify unchanged after the fix.
3. **Missing-field preservation**: Observe `VALIDATION_MISSING_REQUIRED_FIELD` for missing
   `mfa_token`/`code`/`client_id`/`redirect_uri` on unfixed code, then verify unchanged after the
   fix.
4. **Non-MFA login preservation**: Observe that `SessionController::login` for non-MFA users
   performs no pending-column writes and behaves identically on unfixed code, then verify unchanged
   after the fix.

### Unit Tests

- `verifyLogin` rejects an unregistered `client_id` (Property 1).
- `verifyLogin` rejects a non-whitelisted `redirect_uri` for a registered client (Property 2).
- `verifyLogin` rejects a mismatched pending binding even when client/redirect_uri are independently
  valid (Property 3).
- `verifyLogin` succeeds and clears the pending binding to `NULL` on a fully matching request
  (Properties 4, 5).
- `SessionController::login` persists `mfa_pending_client_id`/`mfa_pending_redirect_uri` when
  `authResult->mfaEnabled` is true (Property 6).
- `SessionController::login` does not touch the pending columns when MFA is not enabled
  (Property 7).
- `verifyLogin`'s existing missing-field/wrong-TOTP/unknown-token rejections are unchanged
  (Property 7).

### Property-Based Tests

Following this project's `DROGON_TEST` house convention
`[Category]_[Priority]_[Module]_[Feature]_[Scenario]` (enforced by
`tools/test/scripts/naming_validator.sh`), name new bugfix tests
`Integration_P1_MfaCrossClientAuthFix_PropertyN_*` — the `PropertyN` token sits in the
Feature slot, numbered per the Correctness Properties above, e.g.
`Integration_P1_MfaCrossClientAuthFix_Property1_UnregisteredClientRejected`,
`Integration_P1_MfaCrossClientAuthFix_Property3_MismatchedPendingBindingRejected`,
`Integration_P1_MfaCrossClientAuthFix_Property4_MatchingBindingPreservesTokenIssuance` — so
`tasks.md` can reference them 1:1 with the Correctness Properties section above.

- Generate random registered-client/whitelisted-redirect_uri combinations that do NOT match the
  recorded pending binding and verify all are rejected (Property 3, PBT over the client/redirect_uri
  input domain).
- Generate random valid matching client/redirect_uri pairs across multiple registered clients and
  verify tokens are issued with the frozen response shape and the pending binding is cleared
  afterward (Properties 4, 5).
- Generate random non-buggy inputs (wrong TOTP, missing fields, unknown mfa_token, non-MFA logins)
  across many scenarios and verify byte-for-byte-equivalent behavior to the unfixed baseline
  (Property 7).

### Integration Tests

- Full flow: `SessionController::login` (MFA-enabled user) → `MfaController::verifyLogin` with the
  same client/redirect_uri → tokens issued → pending columns confirmed `NULL` in the database.
- Full flow: `SessionController::login` as `vue-client` → `MfaController::verifyLogin` with
  `admin-console`'s own valid client_id/redirect_uri → rejected with `AUTH_INVALID_CREDENTIALS`,
  confirm no `oauth2_codes`/`oauth2_access_tokens` row was created for that attempt.
- Regression: existing `LoginEnforcementTest.cc`-style flows (non-MFA login, account lockout) remain
  green, confirming the new DB write in `SessionController::login`'s `mfaEnabled` branch does not
  affect any other branch.

## Out of Scope

Per `bugfix.md`'s explicit exclusions, this fix does **not** change:
- `TokenService`/`consumeAuthCode`'s existing redirect_uri equality logic for the general
  authorization_code grant flow (it is correct there; the gap was only that `verifyLogin` never
  invoked the whitelist pre-check).
- Scope validation inside `verifyLogin` (RBAC reads DB user roles, not token scope; scope spoofing
  is not exploitable through this endpoint).
- PKCE support on the MFA second-factor leg (tracked separately; no `code_verifier` is available at
  this step today).
- `mfa_token`'s format, lifetime, or randomness (`std::to_string(internalId)`, unchanged, including
  the accepted concurrent-login-overwrite limitation on the pending-binding columns this fix adds).
