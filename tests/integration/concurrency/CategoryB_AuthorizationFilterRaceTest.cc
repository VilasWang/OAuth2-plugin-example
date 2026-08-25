// tests/integration/concurrency/CategoryB_AuthorizationFilterRaceTest.cc
//
// Spec: concurrency-lifetime-safety-audit — Task 2 (Category B reproduction).
// Property 2: Bug Condition — Data-Race Freedom (TSan) — covers defect 1.4.
//
// ─────────────────────────────────────────────────────────────────────────
// WHAT THIS REPRODUCES (the exact unsynchronized access)
// ─────────────────────────────────────────────────────────────────────────
// Production code (OAuth2Plugin/src/filters/AuthorizationFilter.cc):
//
//     void AuthorizationFilter::loadConfig() {
//         if (initialized_) return;            // (R) racy READ of initialized_
//         ...
//         rules_.push_back(rule);              // (W) racy WRITE: same std::vector
//         ...
//         publicPaths_.push_back(std::regex(...)); // (W) racy WRITE: same vector
//         initialized_ = true;                 // (W) racy WRITE of initialized_
//     }
//
// `loadConfig()` is called at the very top of the public `doFilter()`. There is
// NO mutex / call_once / atomic guarding the check-then-act. When several IO
// threads enter `doFilter()` for the FIRST time concurrently, they all read
// `initialized_ == false`, all fall through the guard, and concurrently
// `push_back` into the SAME `rules_` / `publicPaths_` vectors while also writing
// `initialized_`. That is a textbook data race (concurrent reads+writes of
// `initialized_`, and concurrent mutation of one std::vector).
//
// We drive this through the ONLY public entry point, `doFilter()`, using a
// TOKEN-LESS request: in that case `doFilter()` runs `loadConfig()` (the racy
// part) and then returns 401 WITHOUT touching the OAuth2Plugin — isolating the
// reproduction to the `rules_/publicPaths_/initialized_` race.
//
// ─────────────────────────────────────────────────────────────────────────
// METHODOLOGY (exploratory reproduction on UNFIXED code)
// ─────────────────────────────────────────────────────────────────────────
//   * This test MUST be built with ThreadSanitizer (-fsanitize=thread) to
//     observe the race. Under TSan the concurrent workload trips a
//     "data race" report on `initialized_` / the vectors — the bug-condition
//     test FAILS via the sanitizer, which is the SUCCESS case for an
//     exploratory reproduction on unfixed code.
//   * On a NORMAL (non-instrumented) build there is no race detector, AND
//     concurrently push_back-ing into one vector is undefined behavior that
//     can corrupt the heap / crash. So on a normal build we DO NOT launch the
//     genuine racing workload: we exercise the same production path SERIALLY
//     (full compile coverage, no UB) and the test passes without claiming any
//     race was detected. See ConcurrencyRaceSupport.h for the gating rationale.
//
// **Validates: Requirements 2.4** (design Property 2 — Data-Race Freedom)
//
// _Requirements: 2.4, 2.5, 2.7_

#include <drogon/drogon_test.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <fulla/drogon/filters/AuthorizationFilter.h>

#include "ConcurrencyRaceSupport.h"

using namespace fulla::test::concurrency;
using fulla::drogon::filters::AuthorizationFilter;

namespace
{
// Token-less request: forces doFilter() -> loadConfig() (racy) -> 401 deny,
// without ever reaching the OAuth2Plugin / token validation.
drogon::HttpRequestPtr makeTokenlessRequest()
{
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    // Path is irrelevant: a token-less request is rejected before checkAccess.
    req->setPath("/api/admin/anything");
    return req;
}

// Drives doFilter() once and reports whether the deny (401) callback fired.
// The deny callback ALWAYS fires for a token-less request, so this is a
// reliable, race-free signal that loadConfig() ran and doFilter() completed.
void driveOnce(AuthorizationFilter &filter, std::atomic<int> &denyCount)
{
    auto req = makeTokenlessRequest();
    filter.doFilter(
      req,
      [&denyCount](const drogon::HttpResponsePtr &) {
          denyCount.fetch_add(1, std::memory_order_relaxed);
      },
      []() { /* next/allow callback — not reached for a token-less request */ }
    );
}
}  // namespace

// 1.4 REPRODUCTION: concurrent first-time entry into loadConfig() races the
// check-then-act on initialized_ and the push_back into rules_/publicPaths_.
//
// Scoped-PBT style: each round generates a fresh filter instance and a random
// worker count, producing many distinct concurrent interleavings from a fixed
// seed (reproducible). A fresh instance per round guarantees initialized_ is
// false at entry, so every worker takes the unsynchronized init path.
DROGON_TEST(Integration_P1_Concurrency_1_4_AuthorizationFilter_LoadConfig_DataRace_Repro)
{
    InterleavingGenerator gen(0x0A04C0DEu);

    constexpr int kRounds = 8;
    for (int round = 0; round < kRounds; ++round)
    {
        // Fresh instance => initialized_ == false => racy first-time init path.
        AuthorizationFilter filter;
        std::atomic<int> denyCount{0};

        const int workers = gen.threadCount(/*min*/ 4, /*max*/ 8);

        if (kTsanEnabled)
        {
            // GENUINE RACE (TSan only): release all workers simultaneously so
            // they collide inside the unsynchronized loadConfig() window.
            SpinBarrier barrier(workers);
            std::vector<std::thread> threads;
            threads.reserve(static_cast<size_t>(workers));

            for (int i = 0; i < workers; ++i)
            {
                threads.emplace_back([&filter, &denyCount, &barrier]() {
                    // Pre-build the request so the barrier release lands as
                    // close as possible to the racy loadConfig() entry.
                    auto req = makeTokenlessRequest();
                    barrier.arriveAndWait();
                    filter.doFilter(
                      req,
                      [&denyCount](const drogon::HttpResponsePtr &) {
                          denyCount.fetch_add(1, std::memory_order_relaxed);
                      },
                      []() {}
                    );
                });
            }
            for (auto &t : threads)
            {
                t.join();
            }

            // Every token-less doFilter() must have produced a 401 deny. (Under
            // TSan the race itself is reported out-of-band by the runtime; this
            // assertion just confirms all workers completed the path.)
            CHECK(denyCount.load(std::memory_order_relaxed) == workers);
        }
        else
        {
            // NORMAL BUILD: never launch the genuine vector race (UB). Exercise
            // the identical production path serially for compile+run coverage.
            // This passes deterministically and does NOT claim a race was found.
            driveOnce(filter, denyCount);
            CHECK(denyCount.load(std::memory_order_relaxed) == 1);
        }
    }
}
