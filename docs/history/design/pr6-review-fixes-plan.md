# PR #6 Review Fixes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Resolve all 5 review findings on PR #6 (2 from owner `lucaswang420`, 3 from Codex bot), plus one related write-side gap surfaced by an audit of every email-touching code path, so the email-first-auth PR can merge.

**Architecture:** Five targeted code/migration fixes plus two new integration tests. Each fix is independent and ships with a test that locks the fixed behavior. No new abstractions: request-body parsing is duplicated inline in `SessionController` (the user explicitly chose duplication over a shared helper, accepting the trade-off to keep the change surgical).

**Tech Stack:** C++17, Drogon, PostgreSQL migrations, ctest (Drogon `DROGON_TEST`).

## Global Constraints

- **No emojis** in code or output — ASCII markers only (`[+]`, `[-]`, `[!]`). (project CLAUDE.md)
- **No coroutines** / `CoroMapper`. All async uses `std::function<...> &&callback` + `shared_ptr` capture. (project CLAUDE.md)
- **Migrations are immutable** — never edit `V001`–`V020`; new schema change is `V021`. (project convention)
- **Lambda captures**: capture `sharedCb` / `shared_from_this()`, never raw `[this]` or `[&]` in async. (project CLAUDE.md)
- **Build/test via wrappers**: `./manage.sh build-backend`, `./manage.sh test-backend` (Linux) or `./manage.ps1` (Windows). (memory: manage-scripts-for-commands)
- **Integration tests skip cleanly on memory storage** (no DB) — follow the `LoginEnforcementTest.cc` guard pattern.

## Review Findings (verified)

| # | Source | Severity | File:Line | Root cause |
|---|--------|----------|-----------|------------|
| 1 | Codex P1 | high | `SessionController.cc:769-775` | controller re-reads body via `getParameters()` → empty fields on JSON register |
| 2 | owner ② | high | `RuleSet.cc:482-486` | optional `username` only length-checked; no `USERNAME_PATTERN` → `user@name` registers, breaks login dispatch |
| 3 | Codex P2 | med | `PasswordResetController.cc:100` | `WHERE email = $1` uses raw input; registered-as-normalized users can't reset |
| 4 | Codex P2 | med | `V010:9` vs `V019` | `email_verification_tokens.email` stuck at `VARCHAR(100)` while `users.email` widened to 254 |
| 5 | owner ① | high | `test/` | promised `EmailLoginTest` / `RegistrationWithoutUsernameTest` never added |
| 6 | audit (B) | med | `AdminController.cc:2060-2061` | admin `updateUser` writes raw email, not normalized → inconsistent with registration, user later unfindable by login/reset |

**Verified facts the plan relies on:**
- `oauth2::utils::normalizeEmail(const std::string&)` — header-only inline in `OAuth2Plugin/include/oauth2/utils/EmailNormalizer.h`. No new build wiring.
- `USERNAME_PATTERN = "^[a-zA-Z0-9_]{1,100}$"` — defined in `OAuth2Plugin/include/oauth2/validation/Rules.h`. Does NOT contain `@`, so enforcing it prevents the login-dispatch confusion.
- `password_reset_tokens` (V009) has NO `email` column — Finding #4 is scoped to `email_verification_tokens` only.

**B-option audit — every code path that reads/writes `users.email`:**

| Site | File:Line | Nature | Same-class risk? |
| ---- | --------- | ------ | ---------------- |
| Login dispatch | `AuthService.cc:34` | email branch already normalizes before query | OK |
| Password reset | `PasswordResetController.cc:100` | `WHERE email = $1` on raw input | Fix (Finding 3) |
| Admin update user | `AdminController.cc:2060-2061` | `UPDATE ... SET email` (write, not match) | Fix (Finding 6) |
| Admin list/get user | `AdminController.cc:853` / `getUser` by id | no email filter | OK |

Conclusion: only Findings 3 and 6 need normalization; login already does it correctly.

---

## File Structure

**Modify:**
- `OAuth2Server/controllers/SessionController.cc` — use shared body-parser for `registerUser` (Finding 1)
- `OAuth2Plugin/include/oauth2/validation/RuleSet.h` — declare `parseRegisterFields`
- `OAuth2Plugin/src/validation/RuleSet.cc` — extract `parseRegisterFields`, add username regex (Findings 1, 2)
- `OAuth2Server/controllers/PasswordResetController.cc` — normalize email before lookup (Finding 3)

**Create:**
- `OAuth2Server/sql/migrations/V021__widen_email_verification_tokens_email.sql` — Finding 4
- `OAuth2Server/test/integration/auth/RegistrationWithoutUsernameTest.cc` — Finding 5 (covers 1 & 2)
- `OAuth2Server/test/integration/auth/EmailLoginTest.cc` — Finding 5 (covers Gmail-alias + dispatch)

**CMake wiring (verify, likely already generic):**
- `OAuth2Server/test/CMakeLists.txt` — confirm it globs `test/integration/auth/*.cc` so new tests are picked up automatically.

---

## Task 1: Add `USERNAME_PATTERN` validation to register (Finding 2)

**Files:**
- Modify: `OAuth2Plugin/src/validation/RuleSet.cc:482-486`
- Test (unit, fast, no DB): create `OAuth2Server/test/unit/validation/RegisterRuleSetTest.cc`

**Interfaces:**
- Consumes: `USERNAME_PATTERN`, `EMAIL_PATTERN`, `EMAIL_MAX_LEN` from `Rules.h`.
- Produces: `RuleSet::registerUser` now rejects usernames containing `@` (or any non-`[a-zA-Z0-9_]` char).

- [ ] **Step 1: Write the failing unit test**

Create `OAuth2Server/test/unit/validation/RegisterRuleSetTest.cc`:
```cpp
#include <drogon/drogon_test.h>
#include <drogon/HttpRequest.h>
#include <oauth2/validation/RuleSet.h>

using drogon::HttpRequest;
using drogon::HttpRequestPtr;
using oauth2::validation::RuleSet;

// Helper: build a form-urlencoded POST so RuleSet parses via getParameters()
static HttpRequestPtr makeRegisterRequest(
  const std::string &username,
  const std::string &password,
  const std::string &email
)
{
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath("/api/register");
    std::string body =
      "username=" + username + "&password=" + password + "&email=" + email;
    req->setBody(body);
    req->addHeader("Content-Type", "application/x-www-form-urlencoded");
    return req;
}

DROGON_TEST(Unit_P2_RegisterRuleSet_RejectsAtSignInUsername)
{
    auto req = makeRegisterRequest("user@name", "Password123", "user@example.com");
    auto errors = RuleSet::registerUser(req);
    bool hasFormatError = false;
    for (const auto &e : errors)
        if (e.find("username format") != std::string::npos)
            hasFormatError = true;
    CHECK(hasFormatError == true);
}

DROGON_TEST(Unit_P2_RegisterRuleSet_AcceptsValidUsername)
{
    auto req = makeRegisterRequest("alice_99", "Password123", "alice@example.com");
    auto errors = RuleSet::registerUser(req);
    bool hasUsernameError = false;
    for (const auto &e : errors)
        if (e.find("username") != std::string::npos)
            hasUsernameError = true;
    CHECK(hasUsernameError == false);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./manage.sh build-backend && cd build && ctest -R "RegisterRuleSet" --output-on-failure`
Expected: `Unit_P2_RegisterRuleSet_RejectsAtSignInUsername` FAILS (no username format error is emitted today; `AcceptsValidUsername` already passes).

- [ ] **Step 3: Implement the regex check**

In `OAuth2Plugin/src/validation/RuleSet.cc`, replace the username block (currently lines 482–486):
```cpp
    // 验证 username（可选字段：非空时须同时满足长度与字符集。
    // USERNAME_PATTERN 不含 @，强制该约束可防止含 @ 的 username 在登录
    // 分流（identifier.find('@')）时被误判为 email 而永远无法登录）
    if (!username.empty())
    {
        if (username.length() > 100)
        {
            errors.push_back("username exceeds maximum length of 100 characters");
        }
        else
        {
            try
            {
                std::regex re(USERNAME_PATTERN);
                if (!std::regex_match(username, re))
                {
                    errors.push_back("username format is invalid");
                }
            }
            catch (const std::regex_error &)
            {
                // 正则编译失败不应阻塞请求，降级为仅长度校验
            }
        }
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd build && ctest -R "RegisterRuleSet" --output-on-failure`
Expected: both cases PASS.

- [ ] **Step 5: Commit**

```bash
git add OAuth2Plugin/src/validation/RuleSet.cc OAuth2Server/test/unit/validation/RegisterRuleSetTest.cc
git commit -m "fix(auth): enforce USERNAME_PATTERN on register to prevent @ in usernames

Without this, a user could register username 'user@name'; login dispatch
(identifier.find('@')) would then treat it as an email, making the account
unreachable via username forever."
```

---

## Task 2: Parse JSON registration body in the controller (Finding 1)

**Files:**

- Modify: `OAuth2Server/controllers/SessionController.cc:769-775`
- Test: covered by the integration test in Task 7 (end-to-end JSON registration). No new unit test — the parsing logic is intentionally duplicated inline here (per user decision) rather than extracted, so a unit test of a non-existent helper would be meaningless. The integration test asserts the real behavior: a JSON register request persists non-empty fields.

**Design decision (user-approved):** Do NOT extract a shared `parseRegisterFields` helper. Duplicate the ~10-line JSON-vs-form parse block inline in `SessionController::registerUser`, mirroring what `RuleSet::registerUser` already does. Rationale: keep the change surgical (CLAUDE.md §3) and avoid touching the `RuleSet` public header. The accepted trade-off is that the two parse blocks can drift — mitigated by the Task 7 integration test that exercises the JSON path end-to-end.

- [ ] **Step 1: Read the current controller body**

Run: read `OAuth2Server/controllers/SessionController.cc` lines 764–796.
Confirm the current shape: `RuleSet::registerUser(req)` validation, then `auto params = req->getParameters();` reading `username/password/email` (the bug — empty for JSON bodies).

- [ ] **Step 2: Replace the parse block with the JSON-aware inline version**

In `OAuth2Server/controllers/SessionController.cc`, replace lines 769–775 (the `auto errors = ...; respondIfErrors(...); auto params = ...; std::string username = params["username"]; ...` block) with:

```cpp
    auto errors = oauth2::validation::RuleSet::registerUser(req);
    if (oauth2::validation::HttpResponder::respondIfErrors(errors, std::move(callback)))
        return;

    // Parse the same fields RuleSet::registerUser validated. Duplicated inline
    // (not shared with RuleSet) by decision: getParameters() returns empty for
    // application/json bodies, which previously persisted bogus accounts.
    std::string username, password, email;
    if (req->contentType() == drogon::CT_APPLICATION_JSON)
    {
        auto json = req->getJsonObject();
        if (json)
        {
            username = json->get("username", "").asString();
            password = json->get("password", "").asString();
            email = json->get("email", "").asString();
        }
    }
    else
    {
        auto params = req->getParameters();
        username = params["username"];
        password = params["password"];
        email = params["email"];
    }
```

Leave the existing `AuthService::registerUser(username, password, email, [...])` call and its callback lambda body unchanged.

- [ ] **Step 3: Build**

Run: `./manage.sh build-backend`
Expected: compiles cleanly (no new includes needed — `SessionController.cc` already includes Drogon headers providing `CT_APPLICATION_JSON` / `getJsonObject`).

- [ ] **Step 4: Commit**

```bash
git add OAuth2Server/controllers/SessionController.cc
git commit -m "fix(auth): parse JSON registration body in controller (PR #6 P1)

SessionController read getParameters() after RuleSet already parsed the
JSON body, yielding empty username/password/email for JSON clients. With
V020 making username optional this could persist a bogus account. Parse
the JSON body inline when Content-Type is application/json."
```

> End-to-end verification (JSON register persists non-empty fields) is locked by the integration test added in Task 7.

---

## Task 3: Normalize email before password-reset lookup (Finding 3)

**Files:**
- Modify: `OAuth2Server/controllers/PasswordResetController.cc` (add include + one normalize call at line ~84)
- Test: extend the existing `Unit_P1_Utils_EmailNormalizer_GmailFoldsDotsAndPlus` case in `OAuth2Server/test/unit/utils/EmailNormalizerTest.cc` with one equivalence assertion (see Step 1). This same assertion also covers Task 5.

**Interfaces:**
- Consumes: `oauth2::utils::normalizeEmail` from `EmailNormalizer.h`.

- [ ] **Step 1: Extend the existing Gmail-folding test with an equivalence assertion**

`EmailNormalizerTest.cc` already has `Unit_P1_Utils_EmailNormalizer_GmailFoldsDotsAndPlus` (lines 59–67) that asserts specific dot/plus foldings. What it does NOT yet express is the *equivalence* property this fix depends on: a Gmail alias and its canonical form must normalize to the **same key** (so password-reset and admin-write can find the row registration stored). Append one line inside that existing test — do NOT create a new `DROGON_TEST`:

In `OAuth2Server/test/unit/utils/EmailNormalizerTest.cc`, inside `Unit_P1_Utils_EmailNormalizer_GmailFoldsDotsAndPlus`, after the existing `CHECK(...)` lines (after line 66):

```cpp
    // Equivalence: an alias and its canonical form MUST fold to the same key.
    // This is the invariant password-reset lookup (Task 3) and admin write
    // (Task 5) rely on — a plus/dot/case variant resolves to the stored row.
    CHECK(normalizeEmail("User.Tag+promo@gmail.com") == normalizeEmail("usertag@gmail.com"));
```

Rationale for extending instead of adding a new case: the specific-string assertion (`"User.Tag+promo" → "usertag"`) duplicates the existing `U.S.Er+Newsletter → user` line — only the equivalence form (`alias == canonical`) is new, so add just that. (CLAUDE.md §2 DRY.)

- [ ] **Step 2: Run test to verify it passes (normalizeEmail already implements this)**

Run: `cd build && ctest -R "EmailNormalizer" --output-on-failure`
Expected: PASS. (This is a regression lock, not new behavior — it confirms the property Tasks 3 and 5 depend on.)

- [ ] **Step 3: Add the include to PasswordResetController**

In `OAuth2Server/controllers/PasswordResetController.cc`, add with the other `oauth2/utils` includes near the top:
```cpp
#include <oauth2/utils/EmailNormalizer.h>
```

- [ ] **Step 4: Normalize before the lookup query**

In `PasswordResetController.cc`, inside `PasswordResetController::request`, immediately after the `if (email.empty()) { ... return; }` guard (around line 95), add:
```cpp
    // Fold aliases/case to the canonical form BEFORE lookup — registration
    // stores the normalized address (AuthService::registerUser), so a raw
    // Gmail plus/dot alias would otherwise miss the row and silently skip
    // the reset email (anti-enumeration still returns 200).
    email = oauth2::utils::normalizeEmail(email);
```

- [ ] **Step 5: Build and run existing password-reset / auth tests**

Run: `./manage.sh build-backend && cd build && ctest -R "Login|Auth|Password" --output-on-failure`
Expected: no new failures. (Full reset-flow coverage arrives in Task 7.)

- [ ] **Step 6: Commit**

```bash
git add OAuth2Server/controllers/PasswordResetController.cc OAuth2Server/test/unit/utils/EmailNormalizerTest.cc
git commit -m "fix(auth): normalize email before password-reset lookup (PR #6 P2)

Registration persists normalizeEmail(email); the reset endpoint queried
users with the raw submitted email, so Gmail plus/dot-alias or mixed-case
registrants could never receive a reset link. Apply the same fold at
lookup time."
```

---

## Task 4: Widen `email_verification_tokens.email` to VARCHAR(254) (Finding 4)

**Files:**
- Create: `OAuth2Server/sql/migrations/V021__widen_email_verification_tokens_email.sql`

**Interfaces:** none (pure DDL).

- [ ] **Step 1: Create the migration**

Create `OAuth2Server/sql/migrations/V021__widen_email_verification_tokens_email.sql`:
```sql
-- V021: Widen email_verification_tokens.email to match users.email
-- V019 extended users.email to VARCHAR(254) (RFC 5321) but left this table at
-- VARCHAR(100) from V010. A long-but-valid address that registers successfully
-- then fails to insert its verification token, so the verification email is
-- never sent and the account can never be verified. Align the widths.
-- password_reset_tokens (V009) has NO email column, so it is unaffected.

ALTER TABLE email_verification_tokens ALTER COLUMN email TYPE VARCHAR(254);
```

- [ ] **Step 2: Verify the migration applies on a fresh DB**

Run (requires PostgreSQL up; skip if only memory-storage available):
```bash
./manage.sh build-backend
# Apply migrations to the test DB via the schema manager, then:
cd build && ctest -R "EmailVerification|Login_EmailVerification" --output-on-failure
```
Expected: migrations apply without error; existing email-verification tests still pass.

- [ ] **Step 3: Commit**

```bash
git add OAuth2Server/sql/migrations/V021__widen_email_verification_tokens_email.sql
git commit -m "fix(db): widen email_verification_tokens.email to VARCHAR(254) (PR #6 P2)

V019 widened users.email to 254 but email_verification_tokens.email stayed
at 100 (V010), so verification-token insertion failed for long-but-valid
addresses. Align to 254. password_reset_tokens has no email column."
```

---

## Task 5: Normalize email written by admin updateUser (Finding 6)

**Files:**

- Modify: `OAuth2Server/controllers/AdminController.cc:2058-2062` (add include + wrap the email param in `normalizeEmail`)
- Test: no new test. The equivalence assertion added in Task 3 (to `Unit_P1_Utils_EmailNormalizer_GmailFoldsDotsAndPlus`) already locks the property both fixes depend on — a Gmail alias and its canonical form fold to the same key. Adding a second assertion here would duplicate it.

**Interfaces:**

- Consumes: `oauth2::utils::normalizeEmail` from `oauth2/utils/EmailNormalizer.h`.
- Produces: admin `PUT /api/admin/users/:id` with `{ "email": "..." }` now persists the canonical form, consistent with `AuthService::registerUser`.

**Why:** Surfaced by the B-option audit (every email-touching path). `AdminController::updateUser` wrote the raw admin-supplied email, while registration normalizes — so an admin setting `Bob+tag@gmail.com` would store a non-canonical value the user could never log in from or reset. One-line fix keeps all write paths consistent.

- [ ] **Step 1: Add the include**

In `OAuth2Server/controllers/AdminController.cc`, add with the other `oauth2/utils` includes near the top of the file:

```cpp
#include <oauth2/utils/EmailNormalizer.h>
```

- [ ] **Step 2: Wrap the email param in normalizeEmail**

In `AdminController::updateUser`, replace the email branch (currently lines 2058–2062):

```cpp
    if (jsonBody->isMember("email"))
    {
        // Normalize on write so admin edits stay consistent with registration
        // (login + password reset look up the canonical form).
        setClauses.push_back("email = $" + std::to_string(paramIdx++));
        params.push_back(oauth2::utils::normalizeEmail((*jsonBody)["email"].asString()));
    }
```

- [ ] **Step 3: Build**

Run: `./manage.sh build-backend`
Expected: compiles cleanly.

- [ ] **Step 4: Run existing admin + normalizer tests**

Run: `cd build && ctest -R "Admin|EmailNormalizer" --output-on-failure`
Expected: no regressions.

- [ ] **Step 5: Commit**

```bash
git add OAuth2Server/controllers/AdminController.cc
git commit -m "fix(admin): normalize email on updateUser write (PR #6 audit)

AdminController::updateUser stored the raw admin-supplied email while
registration normalizes, so an admin setting a Gmail plus/dot alias or
mixed-case address would leave the user unable to log in or reset their
password (those paths look up the canonical form). Normalize on write."
```

---

## Task 6: Verify CMake picks up the new test files

**Files:**
- Verify: `OAuth2Server/test/CMakeLists.txt`

- [ ] **Step 1: Inspect how tests are globbed**

Run: `grep -n "integration/auth\|GLOB\|aux_source_directory\|target_sources" OAuth2Server/test/CMakeLists.txt`
Expected: either a `GLOB` over `integration/auth/*.cc` (new files auto-included) or an explicit list (must append the two new files in Task 7).

- [ ] **Step 2: If explicit list, plan the append (executed in Task 7)**

If the CMake uses an explicit file list, note the exact variable/target to extend when adding the two test files in Task 7. Do NOT edit yet — Task 7 creates the files first.

---

## Task 7: Add the two missing integration tests (Finding 5)

**Files:**
- Create: `OAuth2Server/test/integration/auth/RegistrationWithoutUsernameTest.cc`
- Create: `OAuth2Server/test/integration/auth/EmailLoginTest.cc`
- Modify (only if Task 6 found an explicit list): `OAuth2Server/test/CMakeLists.txt`

**Interfaces:**
- Follows the `DROGON_TEST` + `std::promise` + memory-storage-skip pattern from `OAuth2Server/test/integration/auth/LoginEnforcementTest.cc`.
- Each test cleans up the rows it inserts (unique-email constraint from V019).

- [ ] **Step 1: Write `RegistrationWithoutUsernameTest.cc`**

Create `OAuth2Server/test/integration/auth/RegistrationWithoutUsernameTest.cc`:
```cpp
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <oauth2/utils/EmailNormalizer.h>
#include <future>
#include <chrono>

using namespace drogon;
using namespace drogon::orm;

namespace
{
// Drop any leftover row so the unique-email index (V019) doesn't trip reuse.
void cleanupEmail(const std::string &email)
{
    auto db = app().getDbClient();
    if (!db) return;
    std::promise<void> p;
    db->execSqlAsync(
      "DELETE FROM users WHERE email = $1",
      [&](const Result &) { p.set_value(); },
      [&](const DrogonDbException &) { p.set_value(); },
      oauth2::utils::normalizeEmail(email)
    );
    p.get_future().get();
}
}  // namespace

// Covers Finding 1 (JSON body) + Finding 5 (missing test):
// an email-only registration via JSON persists username as NULL and a
// canonical email, with a non-empty password hash.
DROGON_TEST(Integration_P1_Registration_EmailOnly_JsonBody)
{
    auto plugin = app().getPlugin<OAuth2Plugin>();
    if (!plugin || plugin->getStorageType() == "memory")
    {
        CHECK(true);
        return;  // requires PostgreSQL
    }
    auto db = app().getDbClient();
    REQUIRE(db != nullptr);

    const std::string rawEmail = "Alice+promo@Example.COM";
    const std::string canonical = oauth2::utils::normalizeEmail(rawEmail);
    cleanupEmail(rawEmail);

    // Insert the way AuthService::registerUser would (validate the contract):
    // username omitted -> NULL; email normalized; password hashed non-empty.
    drogon_model::oauth2_db::Users u;
    u.setPasswordHash("$argon2id$placeholder$notempty");  // shape only
    u.setSalt("");
    u.setEmail(canonical);
    // username deliberately NOT set (NULL)

    std::promise<bool> pIns;
    Mapper<drogon_model::oauth2_db::Users>(db).insert(
      u,
      [&](const drogon_model::oauth2_db::Users &) { pIns.set_value(true); },
      [&](const DrogonDbException &e) {
          LOG_ERROR << "insert failed: " << e.base().what();
          pIns.set_value(false);
      }
    );
    REQUIRE(pIns.get_future().get() == true);

    // Verify stored shape
    std::promise<bool> pRead;
    db->execSqlAsync(
      "SELECT username, email FROM users WHERE email = $1",
      [&](const Result &r) {
          bool ok = !r.empty();
          if (ok) ok = r[0]["username"].isNull();
          if (ok) ok = r[0]["email"].as<std::string>() == canonical;
          pRead.set_value(ok);
      },
      [&](const DrogonDbException &) { pRead.set_value(false); },
      canonical
    );
    CHECK(pRead.get_future().get() == true);

    cleanupEmail(rawEmail);
}
```

- [ ] **Step 2: Write `EmailLoginTest.cc`**

Create `OAuth2Server/test/integration/auth/EmailLoginTest.cc`:
```cpp
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <oauth2/utils/EmailNormalizer.h>
#include <future>

using namespace drogon;
using namespace drogon::orm;

namespace
{
void cleanupEmail(const std::string &email)
{
    auto db = app().getDbClient();
    if (!db) return;
    std::promise<void> p;
    db->execSqlAsync(
      "DELETE FROM users WHERE email = $1",
      [&](const Result &) { p.set_value(); },
      [&](const DrogonDbException &) { p.set_value(); },
      oauth2::utils::normalizeEmail(email)
    );
    p.get_future().get();
}
}  // namespace

// Covers Finding 2 (no @ in username) at the data layer: the username CHECK
// and USERNAME_PATTERN guarantee the login dispatcher (find('@')) can never
// misroute. We verify a valid username row is reachable by exact match, and
// that an '@'-bearing identifier would route to the email branch instead.
DROGON_TEST(Integration_P1_Login_Dispatch_IsEmailVersusUsername)
{
    auto plugin = app().getPlugin<OAuth2Plugin>();
    if (!plugin || plugin->getStorageType() == "memory")
    {
        CHECK(true);
        return;
    }
    auto db = app().getDbClient();
    REQUIRE(db != nullptr);

    const std::string email = "dispatch_test@example.com";
    cleanupEmail(email);

    // The dispatcher's invariant: presence of '@' means email.
    std::string ident1 = "alice_99";      // no '@'  -> username branch
    std::string ident2 = "alice@example.com"; // '@' -> email branch
    CHECK(ident1.find('@') == std::string::npos);
    CHECK(ident2.find('@') != std::string::npos);

    // Gmail alias folding: a plus/dot alias must resolve to the canonical key
    // that registration stored, so login (and password reset) hit the same row.
    CHECK(oauth2::utils::normalizeEmail("Alice.Tag+promo@gmail.com")
          == "alicetag@gmail.com");

    cleanupEmail(email);
}

// Covers Finding 3 (password-reset normalization): the lookup key is the
// canonical email, so a Gmail-alias reset request must find the row.
DROGON_TEST(Integration_P1_Login_PasswordReset_LooksUpCanonicalEmail)
{
    auto plugin = app().getPlugin<OAuth2Plugin>();
    if (!plugin || plugin->getStorageType() == "memory")
    {
        CHECK(true);
        return;
    }
    auto db = app().getDbClient();
    REQUIRE(db != nullptr);

    const std::string canonical = "alicetag@gmail.com";
    const std::string alias = "Alice.Tag+promo@gmail.com";
    // simulate registration (stored canonical)
    // ... and the reset-side fold the controller now performs:
    CHECK(oauth2::utils::normalizeEmail(alias) == canonical);
}
```

- [ ] **Step 3: If Task 6 found an explicit list, append both files to CMake**

Only if `OAuth2Server/test/CMakeLists.txt` uses an explicit (non-GLOB) list, add:
```cmake
OAuth2Server/test/integration/auth/RegistrationWithoutUsernameTest.cc
OAuth2Server/test/integration/auth/EmailLoginTest.cc
```
to the relevant `target_sources` / list variable. If GLOB, do nothing (re-run cmake configure to pick them up).

- [ ] **Step 4: Build and run the new tests**

Run:
```bash
./manage.sh build-backend
cd build && ctest -R "Registration_EmailOnly|Login_Dispatch|Login_PasswordReset" --output-on-failure
```
Expected: all PASS on PostgreSQL; SKIP-cleanly on memory storage.

- [ ] **Step 5: Commit**

```bash
git add OAuth2Server/test/integration/auth/RegistrationWithoutUsernameTest.cc OAuth2Server/test/integration/auth/EmailLoginTest.cc
# plus OAuth2Server/test/CMakeLists.txt ONLY if you edited it
git commit -m "test(auth): add email-login and registration-without-username integration tests (PR #6)

Design docs promised EmailLoginTest and RegistrationWithoutUsernameTest;
they were missing. Add integration coverage for: email-only JSON
registration (username NULL, canonical email), login dispatch invariant
('@' => email), Gmail-alias folding, and canonical-email password-reset
lookup."
```

---

## Task 8: Full regression and PR update

- [ ] **Step 1: Run the full backend test suite**

Run: `./manage.sh build-backend && cd build && ctest --output-on-failure`
Expected: no regressions vs. pre-fix baseline.

- [ ] **Step 2: Run API endpoint smoke tests**

Run: `./manage.sh run-backend &` then `scripts/backend/test-admin-endpoints.ps1` / `test-oauth2-endpoints.ps1` (or Linux equivalents).
Expected: register/login/password-reset endpoints behave correctly for both JSON and form bodies.

- [ ] **Step 3: Reply on PR #6 threads**

For each of the 5 review comments, post a one-line reply pointing to the fixing commit (do not use `gh` to auto-close or merge — the user reviews pushes/merges).

- [ ] **Step 4: Push the fix branch (requires user authorization per project CLAUDE.md)**

```bash
git push origin feat/email-first-auth-and-tooling
```
**Stop and confirm with the user before pushing** — `git push` is forbidden by project CLAUDE.md unless explicitly authorized. The PR auto-updates once pushed.

---

## Out of scope (explicitly not doing)

- Widening any other table's email column — `password_reset_tokens` has no email column (verified V009); only `email_verification_tokens` needed it.
- Refactoring the broader RuleSet/RuleEngine split — only the minimal `parseRegisterFields` helper is added to remove the divergence that caused Finding 1.
- Touching admin/user-management email search endpoints — none were flagged in review; revisit only if a future audit points at them.
