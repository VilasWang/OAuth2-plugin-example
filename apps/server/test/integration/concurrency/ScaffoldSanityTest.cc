// OAuth2Server/test/integration/concurrency/ScaffoldSanityTest.cc
//
// Wave-1 scaffolding sanity test for the concurrency & lifetime safety audit
// (spec: concurrency-lifetime-safety-audit, Task 0).
//
// PURPOSE (scaffolding only — NOT a defect reproduction):
//   * Verify that the new test directory
//       OAuth2Server/test/integration/concurrency/
//     is picked up by the GLOB_RECURSE in OAuth2Server/test/CMakeLists.txt
//     (INTEGRATION_TESTS) and compiled into the OAuth2Test_test target.
//   * Exercise the build/test pipeline end-to-end under both the normal build
//     and the TSan / ASan sanitizer builds (OAUTH2_SANITIZER=thread|address).
//
// This file deliberately asserts NOTHING about the audited defects (1.1–1.11).
// The actual concurrency / shutdown race reproductions land in later tasks
// (spec tasks 2 and 3). Keeping a trivial-but-real concurrent workload here
// also confirms the sanitizer runtimes link and run with a multi-threaded
// program, without expecting any sanitizer findings.

#include <drogon/drogon_test.h>

#include <atomic>
#include <thread>
#include <vector>

// Trivial sanity check: no threads, pure assertion. Confirms the new
// integration/concurrency/ directory is wired into the test target.
DROGON_TEST(Integration_Concurrency_Scaffold_Sanity)
{
    CHECK(1 + 1 == 2);
}

// Minimal, race-free concurrent workload. Each thread increments a shared
// std::atomic<int>, which is correctly synchronized — so this must remain
// clean under ThreadSanitizer and AddressSanitizer. It only verifies that the
// instrumented binary can spawn threads and join them; it is NOT a defect
// reproduction and is expected to PASS in every build configuration.
DROGON_TEST(Integration_Concurrency_Scaffold_AtomicCounter_NoRace)
{
    constexpr int kThreads = 4;
    constexpr int kIncrementsPerThread = 1000;

    std::atomic<int> counter{0};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i)
    {
        workers.emplace_back([&counter, kIncrementsPerThread]() {
            for (int j = 0; j < kIncrementsPerThread; ++j)
            {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto &w : workers)
    {
        w.join();
    }

    CHECK(counter.load(std::memory_order_relaxed) == kThreads * kIncrementsPerThread);
}
