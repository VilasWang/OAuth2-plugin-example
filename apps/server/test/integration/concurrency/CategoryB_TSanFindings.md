# Category B (Data-Race Freedom) — TSan Reproduction Findings

Spec: `concurrency-lifetime-safety-audit` — Task 2.
Property 2: **Bug Condition — Data-Race Freedom** (TSan). Covers defects **1.4** and **1.5**.

> Methodology note: these are *exploratory reproduction* tests that run on the
> **UNFIXED** code (F). A reproduction test that detects the bug (ThreadSanitizer
> reports a data race) is the **SUCCESS** case per the bugfix workflow. No
> production code is changed in this task.

## Files added

| File | Defect | Exercises |
|---|---|---|
| `ConcurrencyRaceSupport.h` | (shared) | TSan detection (`kTsanEnabled`), `SpinBarrier` (simultaneous release), seeded `InterleavingGenerator` (scoped-PBT interleavings). |
| `CategoryB_AuthorizationFilterRaceTest.cc` | **1.4** | Concurrent first-time `AuthorizationFilter::doFilter() → loadConfig()`. |
| `CategoryB_JwkManagerRaceTest.cc` | **1.5** | One `JwkManager::init()` writer racing many `signJwt()`/`getJwks()` readers. |

These land under `OAuth2Server/test/integration/concurrency/`, auto-collected by
`GLOB_RECURSE INTEGRATION_TESTS` in `OAuth2Server/test/CMakeLists.txt` and compiled
into the `OAuth2Test_test` target (ctest name `OAuth2Tests`).

## The exact unsynchronized accesses under audit

### 1.4 — `AuthorizationFilter::loadConfig()` (check-then-act, no lock)

`OAuth2Plugin/src/filters/AuthorizationFilter.cc`:

```cpp
void AuthorizationFilter::loadConfig() {
    if (initialized_) return;                     // (R) racy read of initialized_
    ...
    rules_.push_back(rule);                        // (W) concurrent mutation, same std::vector
    ...
    publicPaths_.push_back(std::regex(...));       // (W) concurrent mutation, same std::vector
    initialized_ = true;                           // (W) racy write of initialized_
}
```

`loadConfig()` is the first statement of the public `doFilter()`. Multiple IO
threads entering `doFilter()` for the first time all read `initialized_ == false`,
fall through the unguarded check, and concurrently `push_back` into the same
`rules_` / `publicPaths_` vectors while writing `initialized_`. The test drives
this through a **token-less request**, which runs `loadConfig()` then returns 401
*before* touching the `OAuth2Plugin` — isolating the reproduction to
`rules_ / publicPaths_ / initialized_`.

### 1.5 — `JwkManager::init()` write vs `signJwt()`/`getJwks()` read (no sync)

`OAuth2Plugin/src/utils/JwkManager.cc`: `init()` writes `rsaKey_` (`void*`),
`kid_` (`std::string`), `initialized_` (`bool`); `signJwt() const` and
`getJwks() const` read all three. No mutex/atomic/enforced happens-before exists
between them — correctness rests on the undocumented "init once at startup,
read-only afterwards" assumption. The test uses exactly **one** `init()` writer
(avoiding a second `EVP_PKEY` allocation into `rsaKey_`) racing **N** readers.

## How the tests reliably trip TSan

- A fresh instance per round guarantees the object is in its pre-init state, so
  every worker takes the unsynchronized init/transition path (the C(X) window).
- `SpinBarrier` releases all workers simultaneously to maximize collision inside
  the unsynchronized window.
- `InterleavingGenerator` (fixed seed) produces many distinct, reproducible
  concurrent configurations ("scoped PBT" without a 3rd-party property library,
  none of which is vendored in this repo).

## Explicitly NOT covered (per task scope)

- **`drogon::CacheMap` cross-loop access is NOT a Category B race case.**
  `CacheMap` carries its own internal mutex, so concurrent cross-thread access
  does not produce a data race. Expecting a TSan race there would self-refute
  under the "cannot-reproduce ⇒ root cause refuted" rule. Its real problem
  (members destroyed with the host object → UAF) is covered by **Task 3
  (Category C / ASan)**.
- **1.7 (`OAuth2Metrics`)** currently only does `LOG_INFO` with no shared
  counter, so there is **no reproducible race**. No reproduction case is written
  for 1.7; it is handled as a guardrail in fix task 7.3.

## Expected TSan counterexamples (to be captured on a TSan-capable build)

When built and run with ThreadSanitizer, TSan is expected to emit
`WARNING: ThreadSanitizer: data race` reports of the following shape (exact
addresses/TIDs/line numbers will vary by build):

**1.4** — racing writes/reads on the filter's members:

```
WARNING: ThreadSanitizer: data race (pid=...)
  Write of size 8 at 0x... by thread T2:
    #0 std::vector<...>::push_back ...
    #1 oauth2::filters::AuthorizationFilter::loadConfig() AuthorizationFilter.cc:35
    #2 oauth2::filters::AuthorizationFilter::doFilter(...) AuthorizationFilter.cc:60
  Previous write of size 8 at 0x... by thread T1:
    #0 std::vector<...>::push_back ...
    #1 oauth2::filters::AuthorizationFilter::loadConfig() AuthorizationFilter.cc:35
  ... (and a race on the bool `initialized_` at AuthorizationFilter.cc:18/50)
```

**1.5** — racing write (init) vs read (sign/getJwks) on `initialized_`/`kid_`/`rsaKey_`:

```
WARNING: ThreadSanitizer: data race (pid=...)
  Read of size 1 at 0x... by thread T3:
    #0 oauth2::JwkManager::signJwt(...) const JwkManager.cc:160
  Previous write of size 1 at 0x... by thread T1:
    #0 oauth2::JwkManager::init(...) JwkManager.cc:38
  ... (and races on std::string `kid_` and void* `rsaKey_`)
```

## Build / run commands for a TSan run

Linux/macOS with a GCC/Clang toolchain (TSan and ASan are mutually exclusive —
this is the TSan build):

```bash
# From the repo root (authforge/)
bash scripts/backend/build.sh --tsan        # == --sanitizer=thread, implies --debug

# Run the full suite (ctest name OAuth2Tests) under TSan:
cd build && ctest --output-on-failure -R OAuth2Tests

# Or run only the two Category B reproductions directly:
./OAuth2Server/test/OAuth2Test_test \
  Integration_Concurrency_1_4_AuthorizationFilter_LoadConfig_DataRace_Repro \
  Integration_Concurrency_1_5_JwkManager_InitVsSign_DataRace_Repro
```

CMake plumbing (already in place from Task 0): `-DOAUTH2_SANITIZER=thread` →
`cmake/Sanitizers.cmake::oauth2_apply_sanitizer()` appends
`-fsanitize=thread -g -fno-omit-frame-pointer` to the `OAuth2Test_test` target's
compile + link options (GCC/Clang + Debug only).

Optional, to make the run fail hard on the first race (recommended for CI gating):

```bash
export TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1"
```

## Verification status (HONEST)

- **Environment limitation (from Task 0):** this host builds with **MSVC**
  (Visual Studio 17 2022); MSVC has **no `-fsanitize=thread` runtime**, and
  Clang targeting the MSVC ABI has no TSan runtime either. `cmake/Sanitizers.cmake`
  deliberately ignores `OAUTH2_SANITIZER=thread` on those toolchains (with a
  warning) so the normal build still succeeds. The WSL box lacks a
  toolchain/Drogon. **Therefore TSan could NOT be executed in this environment.**
- **What the tests do here:** they are written so that on a normal
  (non-instrumented) build they execute the SAME production code paths
  **serially / in the happens-before-correct order** (no UB, no genuine race),
  and pass deterministically. The genuine concurrent racing workload only runs
  when `kTsanEnabled` is true (compiled with `-fsanitize=thread`).
- **Pending confirmation:** the actual TSan data-race observation for 1.4 and
  1.5 must be confirmed on a **Linux/macOS GCC/Clang TSan build** (e.g. CI via
  `scripts/backend/build.sh --tsan`). The race stacks above are the EXPECTED
  shape; they have **not** been observed on this host and are **not** fabricated
  sanitizer output.
- **PBT/bug-condition status:** the bug-condition reproductions for 1.4/1.5 are
  authored to FAIL (detect the race) on the unfixed code under TSan. Because TSan
  is unavailable here, race detection is reported as **pending a TSan-capable
  build**, not as observed.
