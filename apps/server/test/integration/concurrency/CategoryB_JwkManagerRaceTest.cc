// OAuth2Server/test/integration/concurrency/CategoryB_JwkManagerRaceTest.cc
//
// Spec: concurrency-lifetime-safety-audit — Task 2 (Category B reproduction).
// Property 2: Bug Condition — Data-Race Freedom (TSan) — covers defect 1.5.
//
// ─────────────────────────────────────────────────────────────────────────
// WHAT THIS REPRODUCES (the exact unsynchronized access)
// ─────────────────────────────────────────────────────────────────────────
// Production code (OAuth2Plugin/src/utils/JwkManager.cc):
//
//     bool JwkManager::init(const Json::Value&) {     // WRITER
//         ...
//         rsaKey_      = pkey;        // (W) void* member
//         kid_         = "...";       // (W) std::string member
//         initialized_ = true;        // (W) bool member
//     }
//
//     std::string JwkManager::signJwt(const Json::Value&) const {   // READER
//         if (!initialized_ || !rsaKey_) ...   // (R) initialized_, rsaKey_
//         header["kid"] = kid_;                // (R) kid_
//         EVP_PKEY* pkey = (EVP_PKEY*)rsaKey_;  // (R) rsaKey_
//         ...
//     }
//
//     Json::Value JwkManager::getJwks() const {                     // READER
//         if (!initialized_) ...               // (R) initialized_
//         ... uses rsaKey_ / kid_ ...          // (R) rsaKey_, kid_
//     }
//
// There is NO synchronization (no mutex, no atomics, no happens-before that the
// type system enforces) between the writes in init() and the reads in
// signJwt()/getJwks(). The current correctness rests on an UNDOCUMENTED
// "init once at startup, read-only afterwards" assumption. When init() runs
// concurrently with signJwt()/getJwks(), there is a read/write data race on
// `initialized_` (bool), `kid_` (std::string — a non-atomic assignment whose
// internal buffer/length a reader can observe mid-update) and `rsaKey_` (void*).
//
// ─────────────────────────────────────────────────────────────────────────
// METHODOLOGY (exploratory reproduction on UNFIXED code)
// ─────────────────────────────────────────────────────────────────────────
//   * MUST be built with ThreadSanitizer to observe the race. Under TSan the
//     init-vs-read interleaving trips a "data race" report — the bug-condition
//     test FAILS via the sanitizer, which is the SUCCESS case for an
//     exploratory reproduction on unfixed code.
//   * On a NORMAL build there is no detector and racing a std::string
//     assignment against concurrent reads is undefined behavior. So we DO NOT
//     launch the genuine racing workload on a normal build: we exercise the
//     same init/sign/getJwks production path SERIALLY (init fully BEFORE any
//     read — i.e. the ¬C(X) happens-before-correct ordering), which is
//     race-free and passes deterministically without claiming a race.
//
//   We avoid the rsaKey_ double-free hazard (two init() calls each allocating a
//   new EVP_PKEY into rsaKey_ would leak/own-twice) by having EXACTLY ONE
//   writer thread call init() exactly once, concurrently with many readers.
//   That single init() write racing the concurrent reads is sufficient to trip
//   TSan on initialized_/kid_/rsaKey_.
//
// **Validates: Requirements 2.5** (design Property 2 — Data-Race Freedom)
//
// _Requirements: 2.4, 2.5, 2.7_

#include <drogon/drogon_test.h>
#include <json/json.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <authforge/oauth2/jwk/JwkManager.h>

#include "ConcurrencyRaceSupport.h"

using namespace oauth2::test::concurrency;
using authforge::oauth2::JwkManager;

namespace
{
// Minimal claims payload for signJwt().
Json::Value samplePayload()
{
    Json::Value p;
    p["sub"] = "user-123";
    p["iss"] = "https://issuer.example";
    p["aud"] = "test-client";
    return p;
}

// Empty config => init() falls back to generating an ephemeral RSA key
// (dev/test path). This keeps the reproduction self-contained: no PEM file or
// env var required. The ephemeral keygen is the writer side of the race.
Json::Value emptyConfig()
{
    return Json::Value(Json::objectValue);
}
}  // namespace

// 1.5 REPRODUCTION: a single init() writer racing many signJwt()/getJwks()
// readers on the unsynchronized members initialized_ / kid_ / rsaKey_.
//
// Scoped-PBT style: each round uses a fresh (uninitialized) JwkManager and a
// random reader count from a fixed seed, producing many distinct
// writer-vs-reader interleavings. The fresh instance guarantees the readers
// can observe the init() write transition (false->true, empty kid_->set,
// null rsaKey_->set) — exactly the C(X) window.
DROGON_TEST(Integration_Concurrency_1_5_JwkManager_InitVsSign_DataRace_Repro)
{
    InterleavingGenerator gen(0x0B05C0DEu);

    constexpr int kRounds = 8;
    for (int round = 0; round < kRounds; ++round)
    {
        // Fresh, UNINITIALIZED manager: initialized_==false, rsaKey_==nullptr,
        // kid_=="" — readers will race the single init() transition.
        JwkManager jwk;
        std::atomic<int> readsCompleted{0};

        const int readers = gen.threadCount(/*min*/ 3, /*max*/ 6);

        if (kTsanEnabled)
        {
            // GENUINE RACE (TSan only): 1 writer (init once) + N readers
            // (signJwt/getJwks) released together so reads straddle the init
            // write window.
            SpinBarrier barrier(readers + 1);
            std::vector<std::thread> threads;
            threads.reserve(static_cast<size_t>(readers) + 1);

            // Single writer: init() exactly once (no double-allocation of
            // rsaKey_, avoiding a spurious leak/own-twice unrelated to 1.5).
            threads.emplace_back([&jwk, &barrier]() {
                Json::Value cfg = emptyConfig();
                barrier.arriveAndWait();
                jwk.init(cfg);
            });

            // Readers: alternate signJwt() / getJwks(), reading the members the
            // writer is mutating. Return values are intentionally unchecked —
            // mid-init reads may legitimately return "" / empty JWKS; the DEFECT
            // we are surfacing is the unsynchronized memory access (TSan), not a
            // value mismatch.
            for (int i = 0; i < readers; ++i)
            {
                threads.emplace_back([&jwk, &readsCompleted, &barrier, i]() {
                    Json::Value payload = samplePayload();
                    barrier.arriveAndWait();
                    if (i % 2 == 0)
                    {
                        volatile auto jwt = jwk.signJwt(payload);
                        (void)jwt;
                    }
                    else
                    {
                        volatile auto jwks = jwk.getJwks().size();
                        (void)jwks;
                    }
                    readsCompleted.fetch_add(1, std::memory_order_relaxed);
                });
            }

            for (auto &t : threads)
            {
                t.join();
            }

            CHECK(readsCompleted.load(std::memory_order_relaxed) == readers);
            // After all threads join, init() definitely ran => fully usable.
            CHECK(jwk.isInitialized() == true);
        }
        else
        {
            // NORMAL BUILD: enforce the correct happens-before ordering
            // (init fully BEFORE any read). This is ¬C(X): race-free, and the
            // identical production path is compiled + executed for coverage.
            Json::Value cfg = emptyConfig();
            REQUIRE(jwk.init(cfg) == true);

            for (int i = 0; i < readers; ++i)
            {
                if (i % 2 == 0)
                {
                    CHECK(!jwk.signJwt(samplePayload()).empty());
                }
                else
                {
                    CHECK(jwk.getJwks().isMember("keys"));
                }
                readsCompleted.fetch_add(1, std::memory_order_relaxed);
            }
            CHECK(readsCompleted.load(std::memory_order_relaxed) == readers);
        }
    }
}
