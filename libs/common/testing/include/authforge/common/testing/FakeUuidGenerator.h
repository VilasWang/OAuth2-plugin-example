#pragma once

// Task 15 (authforge-sdk-refactor, design.md §6/§8): fake implementations
// of common::ports interfaces. See FakeClock.h for the placement
// rationale.
//
// FakeUuidGenerator: a deterministic IUuidGenerator that returns
// sequential, predictable ids (default: "fake-uuid-0", "fake-uuid-1", ...)
// instead of real random UUIDs, so a test can assert on exact generated
// values (e.g. "this is the 2nd id issued in this test") rather than only
// on shape/uniqueness.

#include <authforge/common/ports/IUuidGenerator.h>

#include <cstdint>
#include <string>

namespace authforge::common::testing
{

class FakeUuidGenerator : public authforge::common::ports::IUuidGenerator
{
  public:
    FakeUuidGenerator() = default;

    /// Construct with a custom prefix (default "fake-uuid-"); generate()
    /// returns "<prefix><counter>", counter starting at 0 and incrementing
    /// on every call.
    explicit FakeUuidGenerator(std::string prefix) : prefix_(std::move(prefix))
    {
    }

    std::string generate() override
    {
        return prefix_ + std::to_string(counter_++);
    }

    /// Number of ids generated so far.
    uint64_t callCount() const
    {
        return counter_;
    }

    /// Reset the counter back to 0.
    void reset()
    {
        counter_ = 0;
    }

  private:
    std::string prefix_ = "fake-uuid-";
    uint64_t counter_ = 0;
};

}  // namespace authforge::common::testing
