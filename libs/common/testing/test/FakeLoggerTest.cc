// Task 15 (authforge-sdk-refactor, design.md §6/§8): pure gtest unit tests
// for FakeLogger.

#include <authforge/common/testing/FakeLogger.h>

#include <gtest/gtest.h>

using namespace authforge::common::testing;
using authforge::common::ports::LogLevel;

TEST(FakeLoggerTest, CapturesLogEntriesInOrder)
{
    FakeLogger logger;
    logger.log(LogLevel::Info, "first");
    logger.log(LogLevel::Error, "second");

    ASSERT_EQ(logger.count(), 2u);
    EXPECT_EQ(logger.entries()[0].level, LogLevel::Info);
    EXPECT_EQ(logger.entries()[0].message, "first");
    EXPECT_EQ(logger.entries()[1].level, LogLevel::Error);
    EXPECT_EQ(logger.entries()[1].message, "second");
}

TEST(FakeLoggerTest, HasMessageContainingFindsSubstring)
{
    FakeLogger logger;
    logger.log(LogLevel::Warn, "JwkManager: Failed to load key from /path/to/key");

    EXPECT_TRUE(logger.hasMessageContaining("Failed to load key"));
    EXPECT_FALSE(logger.hasMessageContaining("nonexistent substring"));
}

TEST(FakeLoggerTest, HasMessageContainingWithLevelFilter)
{
    FakeLogger logger;
    logger.log(LogLevel::Info, "informational message");
    logger.log(LogLevel::Error, "error message");

    EXPECT_TRUE(logger.hasMessageContaining(LogLevel::Error, "error"));
    EXPECT_FALSE(logger.hasMessageContaining(LogLevel::Info, "error"));
}

TEST(FakeLoggerTest, ClearRemovesAllEntries)
{
    FakeLogger logger;
    logger.log(LogLevel::Debug, "debug");
    logger.clear();
    EXPECT_EQ(logger.count(), 0u);
}

TEST(FakeLoggerTest, CapturesTraceLevel)
{
    // Regression guard for the six-level LogLevel set: Trace must round-trip
    // through ILogger like any other level (previously the enum lacked Trace
    // and the DrogonLogger adapter silently dropped trace calls).
    FakeLogger logger;
    logger.log(LogLevel::Trace, "finest-grained detail");

    ASSERT_EQ(logger.count(), 1u);
    EXPECT_EQ(logger.entries()[0].level, LogLevel::Trace);
    EXPECT_EQ(logger.entries()[0].message, "finest-grained detail");
    EXPECT_TRUE(logger.hasMessageContaining(LogLevel::Trace, "finest"));
}
