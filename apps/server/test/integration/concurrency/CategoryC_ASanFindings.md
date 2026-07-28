# Category C (No Use-After-Free) — ASan Reproduction Findings

Spec: `concurrency-lifetime-safety-audit` — Task 3.
Property 3: **Bug Condition — No Use-After-Free** (AddressSanitizer).
Covers defects **1.8** (incl. original **1.6** `tokenCache_`/`clientCache_` members),
**1.9**, **1.10**, **1.11**.

> Methodology note: these are *exploratory reproduction* tests that run on the
> **UNFIXED** code (F). A reproduction that surfaces the bug (AddressSanitizer
> reports a `heap-use-after-free`) is the **SUCCESS** case per the bugfix
> workflow. **No production code is changed in this task.**

## Files added

| File | Defect | Exercises |
|---|---|---|
| `CategoryC_DeferredStorageSupport.h` | (shared) | `DeferringStorage` — a real `IOAuth2Storage` that PARKS its callbacks on a test-owned `PendingCallbacks` queue, modelling Drogon `DbClient`/`RedisClient` async deferral (so the owner can be destroyed before the in-flight callback fires). |
| `CategoryC_CachedStorageUafTest.cc` | **1.8** (+ **1.6**) | Real `CachedOAuth2Storage`: `getAccessToken`/`saveAccessToken`/`revokeAccessToken`/`getClient` continuations touch `tokenCache_`/`clientCache_`/`redisClient_` through a dangling `this` after `storage_.reset()`. |
| `CategoryC_ServiceChainUafTest.cc` | **1.9** | Real `TokenService`/`IdentityService`: `exchangeCodeForToken`/`refreshAccessToken`/`ensureSubjectMapping`/`handleFirstTimeLogin` continuations read `this->storage_` after the service is freed. |
| `CategoryC_CleanupServiceUafTest.cc` | **1.10** | `runCleanup()` Redis-callback shape: deferred callback reads `this->running_` / `this->storage_->deleteExpiredData()` through a dangling `this` after the service is freed. |
| `CategoryC_ControllerStoragePtrUafTest.cc` | **1.11** | Controller-style chain captures a RAW `getStorage()` pointer and re-dereferences it across an async hop after `storage_.reset()`. |

Shared TSan/ASan gating + the `PendingCallbacks` queue live in
`ConcurrencyRaceSupport.h` (extended in this task with `kAsanEnabled` and
`PendingCallbacks`).

These land under `OAuth2Server/test/integration/concurrency/`, auto-collected by
`GLOB_RECURSE INTEGRATION_TESTS` in `OAuth2Server/test/CMakeLists.txt` and compiled
into the `OAuth2Test_test` target (ctest name `OAuth2Tests`).

## Why a deferring storage double (and not `MemoryOAuth2Storage`)

Every Category C defect is the SAME timing hazard:

> an object issues an async storage op whose continuation captures a raw `this`
> (or a raw `IOAuth2Storage*`) → the object is destroyed (`storage_.reset()` /
> service released) → the in-flight continuation arrives and dereferences the
> dangling pointer to touch an already-freed member.

`MemoryOAuth2Storage` invokes its callbacks **synchronously / inline**, so its
continuations always run while the issuer is alive — it physically cannot
reproduce the deferral. In production the deferral is done by Drogon's
`DbClient`/`RedisClient`, which store the callback and fire it later from their
loop thread, possibly after the issuer is gone.

`DeferringStorage` is a real `IOAuth2Storage` implementation that models exactly
this: it ENQUEUES each continuation onto a `PendingCallbacks` queue owned by the
test. The test then chooses the destroy-vs-fire ordering. This needs **no
external DB/Redis service** and works for the cache-member defect (1.8/1.6) and
the service-chain defect (1.9) faithfully against the **real** production
classes.

## The exact raw-`this` / raw-pointer captures under audit

### 1.8 (incl. 1.6) — `CachedOAuth2Storage` (`CachedOAuth2Storage.cc`)

Each cached op captures a raw `this` and touches a member in the continuation:

```cpp
// getAccessToken (no-redis path): impl_->getAccessToken(token, [this, token, cb](optToken){
//     ... tokenCache_.insert(token, *optToken, ttl);  // member through dangling this
//     cb(optToken); });
// saveAccessToken: impl_->saveAccessToken(token, [this, token, cb, ttl](){
//     if (!redisClient_ || ttl<=0) { cb(); return; } // member read through dangling this ...});
// revokeAccessToken: impl_->revokeAccessToken(token, revokedBy, [this, token, cb](){
//     tokenCache_.erase(token);                       // member through dangling this ...});
// getClient: impl_->getClient(clientId, [this, clientId, cb](client){
//     if (client) clientCache_.insert(clientId,*client,60); // member through dangling this ...});
```

`CachedOAuth2Storage` does **not** inherit `enable_shared_from_this`, and
production owns it via `std::unique_ptr` (`OAuth2Plugin::storage_`). At
`shutdown()` → `storage_.reset()` the cache members are destroyed; an in-flight
continuation then touches them → UAF. **This is exactly where the `CacheMap`
counterexample must appear — under ASan (member destroyed with the host), NOT
TSan** (CacheMap is internally locked, so it is not a data race).

### 1.9 — `TokenService` / `IdentityService` (`TokenService.cc` / `IdentityService.cc`)

Multi-level chains capture a raw `this` and read `this->storage_` in the
continuation: `exchangeCodeForToken` (validateClient → consumeAuthCode → …),
`refreshAccessToken` (atomicRevokeRefreshToken → getRefreshToken → …),
`ensureSubjectMapping` (getInternalUserId → createSubjectMapping),
`handleFirstTimeLogin` (createUserForExternalLogin → createSubjectMapping).
Neither class inherits `enable_shared_from_this`. Service freed before the
continuation arrives → `this->storage_` read on freed memory → UAF. (The backing
`DeferringStorage` is kept alive, so the freed object is precisely the service.)

### 1.10 — `OAuth2CleanupService::runCleanup()` (`OAuth2CleanupService.cc`)

`runCleanup()` hands Redis callbacks capturing a raw `this`
(`[this](const RedisResult&){ ... storage_->deleteExpiredData(); }` and the
exception branch), reading `this->running_` / `this->storage_`. This is
**inconsistent** with `start()`, which correctly uses `weak_from_this()`.
Service destroyed between Redis dispatch and reply → UAF.

### 1.11 — `OAuth2StandardController` (`OAuth2StandardController.cc`)

`auto storage = plugin->getStorage();` returns a raw `IOAuth2Storage*`
(`storage_.get()`). The `client_credentials` grant captures it and re-uses it
across an async hop (`storage->getClient(...)` → continuation →
`storage2->saveAccessToken(...)`); the `userinfo` chain does likewise. If
`storage_.reset()` runs mid-flight, the captured raw storage pointer dangles and
the next `storage->...` virtual call is a UAF. **Per design 1.11, fixing 1.3
alone does NOT fix this** — `getStorage()` must return a `shared_ptr` AND the
chain must capture it.

## How the tests reliably surface the UAF under ASan

- `DeferringStorage` parks each production continuation on a test-owned queue.
- The destroy-vs-fire ordering is gated on `kAsanEnabled`
  (`__SANITIZE_ADDRESS__` / `__has_feature(address_sanitizer)`):
  - **ASan build:** destroy the owner (host / service / storage) FIRST, then
    `pending.fireAll()` → the first dangling-`this` continuation touches freed
    memory → ASan aborts with `heap-use-after-free`. The bug-condition test
    FAILS via the sanitizer — the intended reproduction result.
  - **Normal build:** `pending.drainAll()` FIRST (owner still alive; multi-level
    chains drained to completion, no UB), then destroy → the SAME production
    path runs against a LIVE object for compile+run coverage and passes
    deterministically WITHOUT claiming any UAF was detected.
- A plain runtime `if (kAsanEnabled)` (NOT `if constexpr`) ensures BOTH
  orderings always compile.

## Faithful-reproduction caveats (documented honestly)

- **1.8 / 1.6 and 1.9 are reproduced against the REAL production classes**
  (`CachedOAuth2Storage`, `TokenService`, `IdentityService`) with no external
  service — `DeferringStorage` supplies the async deferral.
- **1.10** drives `runCleanup()`'s Redis-callback **shape** via a local
  stand-in that captures a raw `this` into a deferred callback and, on fire,
  reads `this->running_` and calls `this->storage_->deleteExpiredData()` —
  byte-for-byte the production access pattern. A faithful reproduction driving
  the *real* `OAuth2CleanupService::runCleanup()` requires a **Redis-backed**
  ASan build, because (a) `runCleanup()` is private and only reachable through
  the `runEvery` timer, and (b) without Redis it runs `deleteExpiredData()`
  **synchronously inline** (no deferral window). The fix task (8.3) and the
  Checkpoint (Task 9) should confirm against the real method on a Redis+ASan
  build.
- **1.11** reproduces the raw-`getStorage()`-pointer-across-async hazard
  directly against a real `IOAuth2Storage` (the controller's HTTP routing is
  irrelevant to the lifetime defect). Driving the literal controller endpoints
  (`/oauth2/token` client_credentials, `/oauth2/userinfo`) with a concurrent
  `shutdown()` is an end-to-end variant for the ASan Checkpoint (Task 9).

## Expected ASan counterexample (to be captured on an ASan-capable build)

When built and run with AddressSanitizer, ASan is expected to emit a
`heap-use-after-free` of the following shape (exact addresses / TIDs / line
numbers vary by build). Example for the 1.8 `getAccessToken` cache-fill:

```
==NNNNN==ERROR: AddressSanitizer: heap-use-after-free on address 0x...
READ of size 8 at 0x... thread T0
    #0 drogon::CacheMap<...>::insert(...) CacheMap.h
    #1 oauth2::CachedOAuth2Storage::getAccessToken(...)::{lambda}::operator()(...) CachedOAuth2Storage.cc:160
    #2 oauth2::test::concurrency::PendingCallbacks::fireAll() ConcurrencyRaceSupport.h
    ...
0x... is located 0 bytes inside of N-byte region freed by thread T0 here:
    #0 operator delete(void*)
    #1 std::unique_ptr<oauth2::CachedOAuth2Storage>::reset(...)
    #2 Integration_Concurrency_1_8_CachedStorage_GetAccessToken_UAF_Repro ...
previously allocated by thread T0 here:
    #0 operator new(unsigned long)
    #1 std::make_unique<oauth2::CachedOAuth2Storage>(...)
```

For 1.9 the freed region is the `TokenService`/`IdentityService` object and the
faulting frame is the continuation reading `this->storage_`. For 1.10 the freed
region is the cleanup service and the read is `this->running_` / `this->storage_`.
For 1.11 the freed region is the storage object and the faulting frame is the
controller-style continuation calling `rawStorage->saveAccessToken(...)`.

These stacks are the EXPECTED shape; they have **not** been observed on this
host and are **NOT fabricated** sanitizer output.

## Build / run commands for an ASan run

Linux/macOS with a GCC/Clang toolchain (TSan and ASan are mutually exclusive —
this is the ASan build):

```bash
# From the repo root (authforge/)
bash scripts/backend/build.sh --asan        # == --sanitizer=address, implies --debug

# Run the full suite (ctest name OAuth2Tests) under ASan:
cd build && ctest --output-on-failure -R OAuth2Tests

# Or run only the Category C reproductions directly:
./OAuth2Server/test/OAuth2Test_test \
  Integration_Concurrency_1_8_CachedStorage_GetAccessToken_UAF_Repro \
  Integration_Concurrency_1_8_CachedStorage_SaveAccessToken_UAF_Repro \
  Integration_Concurrency_1_8_CachedStorage_RevokeAccessToken_UAF_Repro \
  Integration_Concurrency_1_8_CachedStorage_GetClient_ClientCache_UAF_Repro \
  Integration_Concurrency_1_9_TokenService_ExchangeCode_UAF_Repro \
  Integration_Concurrency_1_9_TokenService_RefreshToken_UAF_Repro \
  Integration_Concurrency_1_9_IdentityService_EnsureSubjectMapping_UAF_Repro \
  Integration_Concurrency_1_9_IdentityService_HandleFirstTimeLogin_UAF_Repro \
  Integration_Concurrency_1_10_CleanupService_RunCleanup_RedisCallback_UAF_Repro \
  Integration_Concurrency_1_11_Controller_RawStoragePointer_AcrossAsync_UAF_Repro
```

CMake plumbing (already in place from Task 0): `-DOAUTH2_SANITIZER=address` →
`cmake/Sanitizers.cmake::oauth2_apply_sanitizer()` appends
`-fsanitize=address -g -fno-omit-frame-pointer` to the `OAuth2Test_test` target's
compile + link options (GCC/Clang + Debug only).

Recommended to fail hard / get full reports (CI gating):

```bash
export ASAN_OPTIONS="abort_on_error=1 detect_leaks=0 halt_on_error=1"
```

## Verification status (HONEST)

- **Environment limitation (from Task 0):** this host builds with **MSVC**
  (Visual Studio 17 2022). Clang here targets the MSVC ABI; the project's
  `cmake/Sanitizers.cmake` deliberately ignores `OAUTH2_SANITIZER=address` on
  the MSVC/MSVC-ABI toolchains (with a warning) so the normal build still
  succeeds, and there is no usable `-fsanitize=address` runtime in this setup.
  The WSL box lacks a toolchain/Drogon. **Therefore ASan could NOT be executed
  in this environment.**
- **What ran/compiled here:** the tests compile and run under the normal MSVC
  Debug build with `kAsanEnabled == false`, taking the **lifetime-safe ordering**
  (`drainAll()` before destroy). They exercise the identical production code
  paths against LIVE objects (no UB) and pass deterministically. This gives
  compile + run coverage of all five reproductions on this host.
- **What needs an ASan-capable environment:** the actual `heap-use-after-free`
  observation for 1.8 (+1.6) / 1.9 / 1.10 / 1.11 must be confirmed on a
  **Linux/macOS GCC/Clang ASan build** (CI via `scripts/backend/build.sh --asan`).
  On that build the destroy-first ordering executes the genuine UAF and ASan is
  expected to abort with the stacks shown above. 1.10's *real* `runCleanup()`
  and 1.11's *literal* controller endpoints additionally want a **Redis-backed**
  ASan run (see caveats).
- **PBT/bug-condition status:** the bug-condition reproductions are authored to
  FAIL (surface the UAF) on the unfixed code under ASan. Because ASan is
  unavailable here, UAF detection is reported as **pending an ASan-capable
  build**, not as observed. No sanitizer output is fabricated.
