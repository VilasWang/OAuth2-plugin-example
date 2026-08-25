#pragma once

// Task 15 (fulla-sdk-refactor, design.md §6/§8): fake implementations
// of common::ports interfaces, placed in a dedicated test-support library
// (fulla-common-testing) per design.md's instruction "生产实现放
// Adapter；假实现放测试支持库" -- i.e. these are NOT part of libs/common
// itself (which only declares the ports) and NOT part of OAuth2Plugin's
// production Adapter implementations (OpenSslCryptoProvider/
// OpenSslUuidGenerator/SystemClock/DrogonLogger); they are a THIRD,
// test-only library so that:
//   - Production binaries never link test doubles.
//   - Domain-layer unit tests (and later, oauth2/identity Domain tests
//     once those packages exist, M2b/M2.5) can depend on this library
//     for deterministic behavior WITHOUT depending on OAuth2Plugin or any
//     Drogon-touching code -- design.md's acceptance criterion for this
//     task is "Domain 可用假时钟/假 crypto 做确定性单测", and a fake that
//     itself pulled in OAuth2Plugin (Drogon-dependent) would defeat that
//     purpose for the pure Domain packages this is ultimately for.
//
// FakeClock: a controllable IClock for deterministic time-dependent tests
// (TOTP time-step computation, token/code expiry comparisons). Starts at
// an arbitrary but fixed epoch (see kDefaultEpochSeconds) unless
// overridden, and only advances when advanceSeconds()/setSeconds() is
// called -- never via wall-clock reads -- so a test controls time
// completely.

#include <fulla/common/ports/IClock.h>

namespace fulla::common::testing
{

class FakeClock : public fulla::common::ports::IClock
{
  public:
    // Arbitrary fixed default: 2024-01-01T00:00:00Z. Chosen only to be a
    // recognizable, comfortably-in-range Unix timestamp for test assertions
    // (not tied to any real deadline/release date).
    static constexpr int64_t kDefaultEpochSeconds = 1704067200;

    FakeClock() : seconds_(kDefaultEpochSeconds)
    {
    }

    explicit FakeClock(int64_t initialSeconds) : seconds_(initialSeconds)
    {
    }

    int64_t nowSeconds() const override
    {
        return seconds_;
    }

    int64_t nowMilliseconds() const override
    {
        return seconds_ * 1000;
    }

    /// Set the current fake time directly (epoch seconds).
    void setSeconds(int64_t seconds)
    {
        seconds_ = seconds;
    }

    /// Advance the current fake time by `deltaSeconds` (may be negative to
    /// rewind, e.g. to test "just expired" boundary conditions).
    void advanceSeconds(int64_t deltaSeconds)
    {
        seconds_ += deltaSeconds;
    }

  private:
    int64_t seconds_;
};

}  // namespace fulla::common::testing
