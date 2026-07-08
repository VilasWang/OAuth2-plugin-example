// OAuth2Server/test/integration/concurrency/CategoryC_DeferredClientRepositorySupport.h
//
// Spec: authforge-sdk-refactor — Task 11 (CachedClientRepository re-arch).
// New test double for the per-repository cache decorator introduced by
// Task 11. `DeferringStorage` in CategoryC_DeferredStorageSupport.h (kept
// UNCHANGED per Task 11's instructions) implements the full IOAuth2Storage
// god interface and cannot be passed to CachedClientRepository's constructor,
// which only accepts a std::shared_ptr<IClientRepository>. This header adds
// a narrow test double, `DeferringClientRepository`, that implements ONLY
// IClientRepository using the exact same deferred-callback technique as
// DeferringStorage (park the continuation on the test-owned
// `PendingCallbacks` queue instead of invoking it inline), so the new
// CachedClientRepository UAF regression test
// (CategoryC_CachedClientRepositoryUafTest.cc) can model the same
// Postgres/Redis-style async deferral without a real DB.
//
// Placement rationale: this lives in its own header (not appended to
// CategoryC_DeferredStorageSupport.h) because that file is explicitly
// off-limits for edits in this task (it backs the existing regression gate,
// CategoryC_CachedStorageUafTest.cc, and must stay byte-for-byte as the
// gate's baseline). A new, separate header keeps that guarantee mechanical
// (nothing to review for accidental drift) while still reusing its
// `PendingCallbacks` / `makeLiveClient` helpers via #include.
//
// _Requirements: design.md §7.4 (H3/A1) — the decorator's
// enable_shared_from_this + `self` capture pattern must be re-verified on
// the new per-repository shape, using the same "destroy host vs. fire
// callback" race harness as the existing gate.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <oauth2/storage/IClientRepository.h>

#include "CategoryC_DeferredStorageSupport.h"  // PendingCallbacks, makeLiveClient
#include "ConcurrencyRaceSupport.h"

namespace oauth2::test::concurrency
{
// DeferringClientRepository -- IClientRepository whose callbacks are
// deferred (queued), modelling Drogon's DbClient/RedisClient async dispatch,
// analogous to DeferringStorage but scoped to only the client aggregate.
class DeferringClientRepository : public oauth2::IClientRepository
{
  public:
    explicit DeferringClientRepository(std::shared_ptr<PendingCallbacks> pending)
        : pending_(std::move(pending))
    {
    }

    void getClient(const std::string &clientId, ClientCallback &&cb) override
    {
        // A live (non-expiring-relevant) client value -> drives the
        // production CachedClientRepository::getClient continuation into its
        // "cache fill" branch, which touches the clientCache_ member through
        // the captured `this`.
        auto client = makeLiveClient(clientId);
        pending_->enqueue([cb = std::move(cb), client]() {
            cb(std::optional<oauth2::OAuth2Client>(client));
        });
    }

    void validateClient(
      const std::string & /*clientId*/,
      const std::string & /*clientSecret*/,
      BoolCallback &&cb
    ) override
    {
        pending_->enqueue([cb = std::move(cb)]() { cb(true); });
    }

  private:
    std::shared_ptr<PendingCallbacks> pending_;
};
}  // namespace oauth2::test::concurrency
