// Task 15 (fulla-sdk-refactor, design.md §6/§8): pure gtest unit tests
// for FakeUuidGenerator.

#include <fulla/common/testing/FakeUuidGenerator.h>

#include <gtest/gtest.h>

using namespace fulla::common::testing;

TEST(FakeUuidGeneratorTest, GeneratesSequentialDefaultPrefixedIds)
{
    FakeUuidGenerator generator;
    EXPECT_EQ(generator.generate(), "fake-uuid-0");
    EXPECT_EQ(generator.generate(), "fake-uuid-1");
    EXPECT_EQ(generator.generate(), "fake-uuid-2");
}

TEST(FakeUuidGeneratorTest, CustomPrefix)
{
    FakeUuidGenerator generator("req-");
    EXPECT_EQ(generator.generate(), "req-0");
    EXPECT_EQ(generator.generate(), "req-1");
}

TEST(FakeUuidGeneratorTest, CallCountTracksGenerations)
{
    FakeUuidGenerator generator;
    EXPECT_EQ(generator.callCount(), 0u);
    generator.generate();
    generator.generate();
    EXPECT_EQ(generator.callCount(), 2u);
}

TEST(FakeUuidGeneratorTest, ResetRestartsCounter)
{
    FakeUuidGenerator generator;
    generator.generate();
    generator.generate();
    generator.reset();
    EXPECT_EQ(generator.generate(), "fake-uuid-0");
}
