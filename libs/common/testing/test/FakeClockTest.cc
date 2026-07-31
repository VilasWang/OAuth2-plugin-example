// Task 15 (authforge-sdk-refactor, design.md §6/§8): pure gtest unit tests
// proving FakeClock gives Domain code deterministic, controllable time.

#include <authforge/common/testing/FakeClock.h>

#include <gtest/gtest.h>

using namespace authforge::common::testing;

TEST(FakeClockTest, DefaultsToFixedEpoch)
{
    FakeClock clock;
    EXPECT_EQ(clock.nowSeconds(), FakeClock::kDefaultEpochSeconds);
}

TEST(FakeClockTest, ConstructsWithCustomInitialTime)
{
    FakeClock clock(1000);
    EXPECT_EQ(clock.nowSeconds(), 1000);
}

TEST(FakeClockTest, SetSecondsOverridesTime)
{
    FakeClock clock;
    clock.setSeconds(500);
    EXPECT_EQ(clock.nowSeconds(), 500);
}

TEST(FakeClockTest, AdvanceSecondsMovesTimeForward)
{
    FakeClock clock(1000);
    clock.advanceSeconds(30);
    EXPECT_EQ(clock.nowSeconds(), 1030);
}

TEST(FakeClockTest, AdvanceSecondsCanRewind)
{
    FakeClock clock(1000);
    clock.advanceSeconds(-100);
    EXPECT_EQ(clock.nowSeconds(), 900);
}

TEST(FakeClockTest, NowMillisecondsIsSecondsTimesThousand)
{
    FakeClock clock(42);
    EXPECT_EQ(clock.nowMilliseconds(), 42000);
}

TEST(FakeClockTest, NeverAdvancesOnItsOwn)
{
    // The whole point: unlike SystemClock, repeated reads never change
    // without an explicit setSeconds()/advanceSeconds() call -- this is
    // what "deterministic" means for a test exercising e.g. TOTP time-step
    // or token-expiry logic.
    FakeClock clock(777);
    for (int i = 0; i < 1000; ++i)
    {
        EXPECT_EQ(clock.nowSeconds(), 777);
    }
}

// Demonstrates the actual acceptance criterion: a hypothetical "is this
// expired" check written against IClock (not against a hardcoded
// std::chrono call) can be driven deterministically through an expiry
// boundary.
TEST(FakeClockTest, DrivesExpiryBoundaryDeterministically)
{
    auto isExpired = [](const authforge::common::ports::IClock &clock, int64_t expiresAt) {
        return clock.nowSeconds() > expiresAt;
    };

    FakeClock clock(1000);
    const int64_t expiresAt = 1010;

    EXPECT_FALSE(isExpired(clock, expiresAt));  // 1000 <= 1010

    clock.advanceSeconds(10);
    EXPECT_FALSE(isExpired(clock, expiresAt));  // 1010 <= 1010 (not strictly after)

    clock.advanceSeconds(1);
    EXPECT_TRUE(isExpired(clock, expiresAt));  // 1011 > 1010
}
