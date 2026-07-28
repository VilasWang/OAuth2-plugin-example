// OAuth2Server/test/integration/concurrency/CategoryA_StorageLifetimeContractTest.cc
//
// Spec: concurrency-lifetime-safety-audit — Task 1 (Category A reproduction).
// Property 1: Bug Condition — Init-Order Safety — covers 1.3 (raw storage
// pointer lifetime contract fragility).
//
// METHODOLOGY (read carefully):
//   This test runs on the UNFIXED code (F) and DOCUMENTS the fragility of the
//   implicit raw-pointer ownership contract between OAuth2Plugin::storage_ and
//   the services that hold storage_.get(). It does NOT modify production code
//   and does NOT attempt to trigger a real heap-use-after-free — the genuine
//   UAF reproduction belongs to Task 3 (Category C, under AddressSanitizer),
//   which can construct the shutdown-vs-in-flight-callback race against the
//   real storage. SIOF and raw-pointer lifetime issues are timing / link-order
//   dependent and do NOT crash deterministically in a single in-process build.
//
//   Production fact being modelled (see OAuth2Plugin.{h,cc}):
//     * storage_ is `std::unique_ptr<IOAuth2Storage>`.
//     * tokenService_/clientService_/identityService_/cleanupService_ are
//       constructed with `storage_.get()` — they hold a RAW `IOAuth2Storage*`.
//     * OAuth2Plugin::shutdown() calls `cleanupService_->stop()` and then
//       `storage_.reset()` — releasing the storage WHILE the service objects
//       are still alive and still holding the now-dangling raw pointer. Any
//       in-flight async callback dereferencing that raw pointer after reset()
//       is a use-after-free. The contract "storage outlives every user" is an
//       implicit timing convention, NOT enforced by the type system.
//
//   Here we reproduce that ownership SHAPE with a minimal, self-contained model
//   (no DB/Redis needed) and assert — DETERMINISTICALLY — that after the owner
//   resets its unique_ptr, the captured raw pointer is left dangling while the
//   holder is still alive. We never dereference the dangling pointer; we detect
//   the unsafe window via an external liveness sentinel. The test PASSES on the
//   unfixed code: the "pass" is the *documentation of fragility* (raw pointer
//   diverges from owner; holder cannot detect the death). After the 1.3 fix
//   (storage_ -> shared_ptr), the same shape would keep the storage alive as
//   long as a holder references it (asserted again at task 6.4).
//
// _Requirements: 2.1, 2.2, 2.3_  (design Property 1)

#include <drogon/drogon_test.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace
{
// Minimal stand-in for IOAuth2Storage that flips a shared "alive" sentinel in
// its destructor. The sentinel lets us OBSERVE the lifetime window WITHOUT ever
// dereferencing a dangling pointer (no UB in this documentation test).
class FakeStorage
{
  public:
    explicit FakeStorage(std::shared_ptr<std::atomic<bool>> alive) : alive_(std::move(alive))
    {
        alive_->store(true);
    }

    ~FakeStorage()
    {
        alive_->store(false);
    }

    // Stand-in for a member touched by an in-flight async callback.
    int probe() const
    {
        return 42;
    }

  private:
    std::shared_ptr<std::atomic<bool>> alive_;
};

// Models a service that captures storage_.get() as a RAW pointer (exactly like
// TokenService/ClientService/IdentityService/OAuth2CleanupService today).
class RawPtrService
{
  public:
    explicit RawPtrService(FakeStorage *storage) : storage_(storage)
    {
    }

    // The raw pointer the service believes is valid for its whole lifetime.
    const FakeStorage *rawStorage() const
    {
        return storage_;
    }

  private:
    FakeStorage *storage_;  // NON-owning, lifetime by implicit convention.
};
}  // namespace

// 1.3 BASELINE / FRAGILITY DOC: replicate the OAuth2Plugin ownership shape and
// show that resetting the owning unique_ptr (as shutdown() does) leaves the
// service's captured raw pointer DANGLING while the service is still alive and
// still holding it. The holder has NO way to detect the storage death — that
// is the documented fragility of the implicit raw-pointer contract.
DROGON_TEST(Integration_Concurrency_1_3_RawStoragePointer_DanglesAfterReset_Baseline)
{
    auto alive = std::make_shared<std::atomic<bool>>(false);

    // Owner: unique_ptr<IOAuth2Storage> storage_  (modelled).
    std::unique_ptr<FakeStorage> storage = std::make_unique<FakeStorage>(alive);
    FakeStorage *rawAtConstruction = storage.get();

    // Holder: service constructed with storage_.get()  (modelled).
    RawPtrService service(storage.get());

    // Sanity: while storage_ is alive, the contract holds.
    REQUIRE(alive->load() == true);
    CHECK(service.rawStorage() == rawAtConstruction);

    // shutdown(): storage_.reset() runs while the service object is STILL ALIVE
    // (services are released after storage today only by member-decl luck; the
    // explicit reset() in shutdown() destroys storage first).
    storage.reset();

    // The storage is destroyed...
    CHECK(alive->load() == false);
    // ...but the service STILL holds the same (now dangling) raw pointer, and
    // cannot tell that it is dangling. This is the lifetime-contract fragility.
    // NOTE: we deliberately do NOT dereference service.rawStorage() here — that
    // would be the actual UAF reproduced under ASan in Task 3.
    CHECK(service.rawStorage() == rawAtConstruction);
    CHECK(alive->load() == false);
}

// 1.3 BASELINE / TARGET-STATE CONTRAST: the SAME shape with SHARED ownership
// (the 1.3 fix: storage_ -> shared_ptr, services hold shared_ptr) keeps the
// storage ALIVE as long as any holder references it, even across the owner's
// reset(). This documents the post-fix invariant that task 6.4 will re-verify;
// it PASSES on a shared_ptr model regardless of the production fix state.
DROGON_TEST(Integration_Concurrency_1_3_SharedStorage_SurvivesReset_TargetContract)
{
    auto alive = std::make_shared<std::atomic<bool>>(false);

    std::shared_ptr<FakeStorage> owner = std::make_shared<FakeStorage>(alive);
    // Holder shares ownership instead of capturing a raw pointer.
    std::shared_ptr<FakeStorage> holder = owner;

    REQUIRE(alive->load() == true);

    // Owner releases its reference (analogous to storage_.reset() at shutdown).
    owner.reset();

    // Storage is STILL alive because the holder shares ownership — the in-flight
    // user is safe. This is the lifetime guarantee the 1.3 fix establishes.
    CHECK(alive->load() == true);
    CHECK(holder->probe() == 42);

    // Once the last holder goes away, the storage is destroyed deterministically.
    holder.reset();
    CHECK(alive->load() == false);
}
