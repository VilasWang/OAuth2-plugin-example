// tests/integration/concurrency/CategoryC_DeferredClientRepositorySupport.h
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
// Phase 4.4 (authforge-sdk-refactor): CachedClientRepository now implements
// the NEW Domain interface (authforge::oauth2::repository::IClientRepository),
// so DeferringClientRepository implements the new interface too (was the
// legacy oauth2::IClientRepository). It builds the new-model OAuth2Client
// locally via makeLiveClientModel below -- the shared
// CategoryC_DeferredStorageSupport.h::makeLiveClient still returns the legacy
// DTO for the god-facade-based CategoryC_*UafTest files (retired in 4.6/4.7).
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

#include <authforge/oauth2/repository/IClientRepository.h>
#include <authforge/oauth2/model/Client.h>
#include <authforge/oauth2/model/ClientType.h>

#include "ConcurrencyRaceSupport.h"  // PendingCallbacks

namespace authforge::test::concurrency
{
// Build a live new-model OAuth2Client (drives the production
// CachedClientRepository::getClient continuation into its "cache fill" branch,
// which touches clientCache_ through the captured `this`).
inline ::authforge::oauth2::model::OAuth2Client makeLiveClientModel(const std::string &clientId)
{
    ::authforge::oauth2::model::OAuth2Client c;
    c.clientId = clientId;
    c.clientType = ::authforge::oauth2::model::ClientType::CONFIDENTIAL;
    c.clientSecretHash = "hash";
    c.salt = "salt";
    c.redirectUris = {"https://example.test/cb"};
    c.allowedScopes = {"openid"};
    return c;
}

// DeferringClientRepository -- the NEW IClientRepository whose callbacks are
// deferred (queued), modelling Drogon's DbClient/RedisClient async dispatch,
// analogous to DeferringStorage but scoped to only the client aggregate.
class DeferringClientRepository : public ::authforge::oauth2::repository::IClientRepository
{
  public:
    explicit DeferringClientRepository(std::shared_ptr<PendingCallbacks> pending)
        : pending_(std::move(pending))
    {
    }

    void getClient(
      const std::string &clientId,
      ::authforge::oauth2::repository::IClientRepository::ClientCallback &&cb
    ) override
    {
        // A live client value -> drives the production
        // CachedClientRepository::getClient continuation into its "cache fill"
        // branch, which touches the clientCache_ member through the captured
        // `this`.
        auto client = makeLiveClientModel(clientId);
        pending_->enqueue([cb = std::move(cb), client]() {
            cb(std::optional<::authforge::oauth2::model::OAuth2Client>(client));
        });
    }

    void validateClient(
      const std::string & /*clientId*/,
      const std::string & /*clientSecret*/,
      ::authforge::oauth2::repository::IClientRepository::BoolCallback &&cb
    ) override
    {
        pending_->enqueue([cb = std::move(cb)]() { cb(true); });
    }

  private:
    std::shared_ptr<PendingCallbacks> pending_;
};
}  // namespace authforge::test::concurrency
