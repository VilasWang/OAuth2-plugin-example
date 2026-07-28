// OAuth2Server/test/integration/concurrency/ConcurrencyRaceSupport.h
//
// Spec: concurrency-lifetime-safety-audit — Task 2 (Category B reproduction).
// Shared support utilities for the ThreadSanitizer (TSan) data-race
// reproduction tests (defects 1.4 and 1.5).
//
// This header is intentionally header-only and race-free in itself: every
// piece of test-side shared state uses std::atomic and all worker threads are
// joined before any result is read on the main thread. The ONLY data race a
// TSan build should report from these tests is the *production* race under
// audit (AuthorizationFilter::loadConfig for 1.4, JwkManager init/read for
// 1.5) — never the harness.
//
// ─────────────────────────────────────────────────────────────────────────
// TSan gating (OAUTH2_TSAN_ENABLED / kTsanEnabled)
// ─────────────────────────────────────────────────────────────────────────
// These are *reproduction* tests whose detection mechanism is ThreadSanitizer.
// The genuine concurrent path they exercise is, by definition, an unsynchronized
// data race in the UNFIXED production code:
//   * 1.4 races concurrent std::vector::push_back on the same vector — that is
//     undefined behavior that CAN corrupt the heap and crash even WITHOUT a
//     sanitizer on a normal build.
//   * 1.5 races a std::string assignment vs concurrent reads — also UB.
//
// To keep the suite runnable and crash-free on a normal (non-instrumented)
// build while still RELIABLY tripping TSan when built with -fsanitize=thread,
// we decide at RUNTIME (via the compile-time-detected `kTsanEnabled`) whether
// to launch the genuine racing workload:
//   * Under TSan  -> run the real concurrent reproduction (TSan reports the
//                    race; the bug-condition test FAILS, which is the SUCCESS
//                    case for an exploratory reproduction on unfixed code).
//   * Normal build -> run a SAFE, serialized exercise of the same production
//                     code path (compiles the racing code for coverage but
//                     never executes it concurrently), and the test passes
//                     trivially WITHOUT claiming any race was detected.
//
// We use a plain runtime `if (kTsanEnabled)` (NOT `if constexpr`) on purpose,
// so BOTH branches are always compiled — giving the normal build full compile
// coverage of the racing code without ever running it.
//
// ─────────────────────────────────────────────────────────────────────────
// ASan gating (OAUTH2_ASAN_ENABLED / kAsanEnabled) — added for Task 3
// ─────────────────────────────────────────────────────────────────────────
// Task 3 (Category C) reproduces heap-use-after-free under AddressSanitizer:
// an object is destroyed while one of its async callbacks is still in flight,
// and the in-flight callback then dereferences a dangling `this` / raw pointer
// to touch already-freed members. Just like the TSan gating above, the GENUINE
// use-after-free is undefined behavior that can crash / corrupt the heap even
// WITHOUT a sanitizer on a normal build, so we gate it on `kAsanEnabled`:
//   * Under ASan  -> reset the owner BEFORE firing the in-flight callback, so
//                    the callback runs against freed memory; ASan reports
//                    `heap-use-after-free` and aborts — the bug-condition test
//                    FAILS, which is the SUCCESS case for an exploratory
//                    reproduction on unfixed code.
//   * Normal build -> fire the in-flight callback BEFORE the owner is reset
//                     (lifetime-safe ordering): the SAME production code path
//                     runs against a LIVE object (full compile + run coverage,
//                     no UB) and the test passes deterministically WITHOUT
//                     claiming any UAF was detected.
// As with TSan, use a plain runtime `if (kAsanEnabled)` (NOT `if constexpr`) so
// BOTH orderings always compile.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <random>
#include <thread>
#include <utility>
#include <vector>

// Compile-time ThreadSanitizer detection.
#if defined(__SANITIZE_THREAD__)
#define OAUTH2_TSAN_ENABLED 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define OAUTH2_TSAN_ENABLED 1
#endif
#endif
#ifndef OAUTH2_TSAN_ENABLED
#define OAUTH2_TSAN_ENABLED 0
#endif

// Compile-time AddressSanitizer detection (analogue of the TSan macro above).
#if defined(__SANITIZE_ADDRESS__)
#define OAUTH2_ASAN_ENABLED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define OAUTH2_ASAN_ENABLED 1
#endif
#endif
#ifndef OAUTH2_ASAN_ENABLED
#define OAUTH2_ASAN_ENABLED 0
#endif

namespace oauth2::test::concurrency
{
// True only when this translation unit was compiled with -fsanitize=thread.
// Used as a RUNTIME guard so both code paths compile on every build.
inline constexpr bool kTsanEnabled = (OAUTH2_TSAN_ENABLED != 0);

// True only when this translation unit was compiled with -fsanitize=address.
// Used as a RUNTIME guard (Task 3) so both the genuine-UAF ordering and the
// lifetime-safe ordering always compile, but only the ASan build executes the
// real use-after-free.
inline constexpr bool kAsanEnabled = (OAUTH2_ASAN_ENABLED != 0);

// ─────────────────────────────────────────────────────────────────────────
// PendingCallbacks — a tiny external queue of "in-flight async callbacks".
// ─────────────────────────────────────────────────────────────────────────
// Drogon's DbClient / RedisClient defer a captured callback and fire it later,
// from the client's loop thread, possibly AFTER the object that issued the
// request has been destroyed. To reproduce that lifetime hazard
// deterministically and WITHOUT a real DB/Redis, the Category C test doubles
// (see CategoryC_DeferredStorageSupport.h) push the continuation handed to them
// onto a PendingCallbacks queue that lives OUTSIDE the object under test (the
// test owns it via shared_ptr). The test can then destroy the owner and fire
// the queued callback afterwards — exactly the "object destroyed before the
// in-flight callback arrives" timing.
//
// The queue itself is lifetime-safe: it holds std::function thunks and never
// touches the object under test on its own. The ONLY dangling access is the
// captured `this` / raw pointer INSIDE a thunk — which is the production defect
// under audit.
struct PendingCallbacks
{
    std::vector<std::function<void()>> items;

    void enqueue(std::function<void()> thunk)
    {
        items.emplace_back(std::move(thunk));
    }

    std::size_t size() const
    {
        return items.size();
    }

    bool empty() const
    {
        return items.empty();
    }

    // Fire every CURRENTLY queued callback in arrival order. Swap-then-iterate
    // so a thunk that enqueues further work does not invalidate our iteration;
    // any newly enqueued work remains for a subsequent fireAll()/drainAll().
    // A SINGLE fireAll() is all that is needed to trip the genuine UAF under
    // ASan: the very first dangling-`this` continuation touches freed memory.
    void fireAll()
    {
        std::vector<std::function<void()>> local;
        local.swap(items);
        for (auto &thunk : local)
        {
            if (thunk)
            {
                thunk();
            }
        }
    }

    // Fire callbacks until the queue is fully empty. Production async chains are
    // multi-level (one continuation enqueues the next), so the lifetime-SAFE
    // ordering on a normal build must drain the whole chain for the final
    // business callback to run. Bounded to avoid an accidental infinite loop.
    void drainAll()
    {
        for (int guard = 0; guard < 1024 && !items.empty(); ++guard)
        {
            fireAll();
        }
    }
};

// A tiny spin barrier (C++17 — std::latch/std::barrier are C++20 and the
// project builds with CMAKE_CXX_STANDARD=17). It releases all participating
// worker threads as close to simultaneously as possible, maximizing the
// probability of landing inside the unsynchronized check-then-act window of
// the production code under test.
class SpinBarrier
{
  public:
    explicit SpinBarrier(int participants) : remaining_(participants)
    {
    }

    // Each worker calls this once. The last arrival flips `go_`, releasing all
    // spinning threads at once. All barrier state is atomic -> race-free.
    void arriveAndWait()
    {
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            go_.store(true, std::memory_order_release);
        }
        while (!go_.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    }

  private:
    std::atomic<int> remaining_;
    std::atomic<bool> go_{false};
};

// Deterministic, seeded interleaving generator for the "scoped PBT" approach:
// instead of a 3rd-party property library (none is vendored in this repo), we
// generate randomized — but reproducible — concurrent configurations
// (thread counts per round) with a fixed seed. Each generated configuration is
// a distinct interleaving scenario exercised against a FRESH instance of the
// object under test, which is what reliably surfaces the race under TSan.
class InterleavingGenerator
{
  public:
    explicit InterleavingGenerator(std::uint32_t seed) : rng_(seed)
    {
    }

    // Random worker-thread count in [minThreads, maxThreads].
    int threadCount(int minThreads, int maxThreads)
    {
        std::uniform_int_distribution<int> dist(minThreads, maxThreads);
        return dist(rng_);
    }

  private:
    std::mt19937 rng_;
};
}  // namespace oauth2::test::concurrency
