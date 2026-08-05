// tests/integration/concurrency/CategoryC_CachedClientRepositoryUafTest.cc
//
// Spec: authforge-sdk-refactor — Task 11 (缓存装饰器 CachedOAuth2Storage 再架构).
//
// ─────────────────────────────────────────────────────────────────────────
// WHAT THIS VERIFIES
// ─────────────────────────────────────────────────────────────────────────
// Task 11 replaces the "wrap the whole IOAuth2Storage god interface" cache
// decorator shape with a per-repository decorator, CachedClientRepository
// (OAuth2Plugin/include/oauth2/storage/CachedClientRepository.h), that wraps
// only IClientRepository. design.md §7.4 / evaluation A1 require that the
// existing `enable_shared_from_this` + `self`-capture lifetime-safety
// pattern already fixed onto CachedOAuth2Storage (commit `30a1d1e`, defect
// 1.8/1.6) be carried over UNCHANGED onto the new decorator -- this is NOT a
// bugfix task, it is a "did we actually re-derive the safety pattern, or did
// we just declare it and hope" verification.
//
// This is a NEW verification asset added by Task 11, distinct from the
// EXISTING regression gate CategoryC_CachedStorageUafTest.cc (which
// continues to guard CachedOAuth2Storage itself, untouched by this task).
// This file proves the same safety pattern was faithfully re-derived on the
// new per-repository decorator -- it is not itself a regression gate for
// pre-existing behavior (there is no "before" for CachedClientRepository).
//
// ─────────────────────────────────────────────────────────────────────────
// METHODOLOGY (mirrors CategoryC_CachedStorageUafTest.cc's
// Integration_Concurrency_1_8_CachedStorage_GetClient_ClientCache_UAF_Repro)
// ─────────────────────────────────────────────────────────────────────────
// CachedClientRepository::getClient() hands impl_->getClient() a
// continuation that captures `self = shared_from_this()` (a strong
// reference) plus `this`, and the continuation touches the `clientCache_`
// member (cache fill) before invoking the caller's callback.
// `DeferringClientRepository` (CategoryC_DeferredClientRepositorySupport.h)
// backs impl_ and defers its callback onto an external `PendingCallbacks`
// queue the TEST owns (modelling a real Postgres/Redis async dispatch),
// letting the test choose whether the host is destroyed before or after the
// continuation fires:
//   * ASan build (kAsanEnabled): destroy the host FIRST, then fire the
//     continuation. Because the pattern captures a strong `self`, the host
//     (and its clientCache_ member) is kept alive by that strong reference
//     for the duration of the continuation, so NO use-after-free occurs even
//     though the test's own shared_ptr was reset first -- this is the
//     intended, safe outcome that proves the pattern works (unlike
//     CategoryC_CachedStorageUafTest.cc's exploratory-on-unfixed-code
//     framing, there is no "expected failure" case here: CachedClientRepository
//     was written with the fix already in place from day one).
//   * Normal build: fire the continuation while the host is still alive,
//     then destroy -- full path coverage without relying on the sanitizer at
//     all.
//
// **Validates: design.md §7.4 (H3/A1 — per-repository cache decorator
// preserves the enable_shared_from_this + self-capture lifetime pattern)**

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>

#include <atomic>
#include <memory>
#include <string>

#include <authforge/storage/redis/CachedClientRepository.h>

#include "CategoryC_DeferredClientRepositorySupport.h"
#include "ConcurrencyRaceSupport.h"

using namespace authforge::test::concurrency;
using authforge::storage::redis::CachedClientRepository;

namespace
{
// Same destroy-vs-callback race selection as
// CategoryC_CachedStorageUafTest.cc's runDestroyVsCallbackRace: under ASan we
// destroy the host FIRST so the strong `self` capture is put to the test;
// under a normal build we fire the continuation while the host is alive for
// full path coverage without relying on the sanitizer.
void runDestroyVsCallbackRace(const std::function<void()> &destroyHost, PendingCallbacks &pending)
{
    if (kAsanEnabled)
    {
        destroyHost();
        pending.fireAll();
    }
    else
    {
        pending.drainAll();
        destroyHost();
    }
}
}  // namespace

// CachedClientRepository::getClient() -- L1 cache-fill continuation touches
// clientCache_ through `self = shared_from_this()` after the test's owning
// shared_ptr to the host is reset. The strong `self` capture must keep the
// host alive for the continuation's duration -- no use-after-free, even
// under ASan, and the final callback must still fire exactly once.
DROGON_TEST(Integration_P1_Concurrency_11_CachedClientRepository_GetClient_ClientCache_UAF_Repro)
{
    auto pending = std::make_shared<PendingCallbacks>();
    auto host = std::make_shared<CachedClientRepository>(
      std::make_shared<DeferringClientRepository>(pending)
    );

    std::atomic<int> delivered{0};
    host->getClient(
      "client-getc-per-repo",
      [&delivered](std::optional<::authforge::oauth2::model::OAuth2Client>) {
          delivered.fetch_add(1, std::memory_order_relaxed);
      }
    );

    // The impl_ continuation (capturing the host's `self` + `this`) is now
    // parked, mirroring the existing gate's assertion.
    REQUIRE(pending->size() == 1);

    runDestroyVsCallbackRace([&host]() { host.reset(); }, *pending);

    // The final callback fired exactly once, whether the host was destroyed
    // before (ASan: kept alive by `self`) or after (normal build) the
    // continuation ran.
    CHECK(delivered.load(std::memory_order_relaxed) == 1);
}

// A second getClient() call after the first response is cached must be
// served from clientCache_ without invoking impl_ again -- basic cache-hit
// coverage for the new per-repository decorator (not a UAF check by itself,
// but exercises the cache path that the UAF test above only reaches via the
// miss path).
DROGON_TEST(Integration_P1_Concurrency_11_CachedClientRepository_GetClient_CacheHit)
{
    auto pending = std::make_shared<PendingCallbacks>();
    auto host = std::make_shared<CachedClientRepository>(
      std::make_shared<DeferringClientRepository>(pending)
    );

    std::atomic<int> delivered{0};
    host->getClient(
      "client-cachehit", [&delivered](std::optional<::authforge::oauth2::model::OAuth2Client>) {
          delivered.fetch_add(1, std::memory_order_relaxed);
      }
    );
    REQUIRE(pending->size() == 1);
    pending->drainAll();
    CHECK(delivered.load(std::memory_order_relaxed) == 1);

    // Second call should hit the L1 cache: no new continuation is parked.
    host->getClient(
      "client-cachehit", [&delivered](std::optional<::authforge::oauth2::model::OAuth2Client>) {
          delivered.fetch_add(1, std::memory_order_relaxed);
      }
    );
    CHECK(pending->empty());
    CHECK(delivered.load(std::memory_order_relaxed) == 2);
}

// ---------------------------------------------------------------------------
// Coverage additions (P2): cache behavior the UAF/CacheHit tests did not
// reach -- a MISS (impl returns nullopt) must NOT be cached, so the next
// call still consults impl_; and validateClient is a pure pass-through
// (never consults/fills the cache).
// ---------------------------------------------------------------------------

// A deferred IClientRepository whose getClient returns Nullopt (a cache MISS
// that must not be cached). Mirrors DeferringClientRepository's shape but
// yields std::nullopt instead of a live client.
class DeferringMissingClientRepository : public ::authforge::oauth2::repository::IClientRepository
{
  public:
    explicit DeferringMissingClientRepository(std::shared_ptr<PendingCallbacks> pending)
        : pending_(std::move(pending))
    {
    }

    void getClient(
      const std::string & /*clientId*/,
      ::authforge::oauth2::repository::IClientRepository::ClientCallback &&cb
    ) override
    {
        ++getClientCalls;
        pending_->enqueue([cb = std::move(cb)]() { cb(std::nullopt); });
    }

    void validateClient(
      const std::string &,
      const std::string &,
      ::authforge::oauth2::repository::IClientRepository::BoolCallback &&cb
    ) override
    {
        pending_->enqueue([cb = std::move(cb)]() { cb(false); });
    }

    int getClientCalls = 0;

  private:
    std::shared_ptr<PendingCallbacks> pending_;
};

// getClient: when impl_ returns nullopt the result must NOT be cached, so a
// second call for the same id still hits impl_ (CachedClientRepository.cc:34
// -- the cache insert is guarded by `if (client)`). Without this guard a
// missing client would poison the cache for 60s.
DROGON_TEST(Integration_P1_Concurrency_11_CachedClientRepository_GetClient_CacheMissNullopt_NotCached)
{
    auto pending = std::make_shared<PendingCallbacks>();
    auto impl = std::make_shared<DeferringMissingClientRepository>(pending);
    auto host = std::make_shared<CachedClientRepository>(impl);

    std::optional<::authforge::oauth2::model::OAuth2Client> first;
    host->getClient(
      "client-missing", [&first](std::optional<::authforge::oauth2::model::OAuth2Client> c) {
          first = std::move(c);
      }
    );
    REQUIRE(pending->size() == 1);
    pending->drainAll();
    CHECK(!first.has_value());
    CHECK(impl->getClientCalls == 1);

    // Second call: a MISS must not have been cached, so impl_ is consulted
    // again (a new continuation is parked).
    std::optional<::authforge::oauth2::model::OAuth2Client> second;
    host->getClient(
      "client-missing", [&second](std::optional<::authforge::oauth2::model::OAuth2Client> c) {
          second = std::move(c);
      }
    );
    CHECK(pending->size() == 1);  // impl_ was hit again, not the cache
    pending->drainAll();
    CHECK(!second.has_value());
    CHECK(impl->getClientCalls == 2);
}

// validateClient: a pure pass-through that never consults the cache. Two
// calls both reach impl_ (no short-circuit on a cached value).
DROGON_TEST(Integration_P1_Concurrency_11_CachedClientRepository_ValidateClient_IsPassThrough)
{
    auto pending = std::make_shared<PendingCallbacks>();
    auto impl = std::make_shared<DeferringClientRepository>(pending);
    auto host = std::make_shared<CachedClientRepository>(impl);

    bool first = true;
    host->validateClient("client-pt", "secret", [&first](bool v) { first = v; });
    REQUIRE(pending->size() == 1);
    pending->drainAll();
    CHECK(first == true);  // DeferringClientRepository.validateClient returns true

    bool second = false;
    host->validateClient("client-pt", "secret", [&second](bool v) { second = v; });
    // validateClient is a pass-through: it parks a second continuation on
    // impl_ (it did NOT short-circuit via any cache).
    CHECK(pending->size() == 1);
    pending->drainAll();
    CHECK(second == true);
}
