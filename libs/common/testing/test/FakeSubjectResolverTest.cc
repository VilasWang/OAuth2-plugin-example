// Task 15 (authforge-sdk-refactor, design.md §6/§8): pure gtest unit tests
// for FakeSubjectResolver.

#include <authforge/common/testing/FakeSubjectResolver.h>

#include <gtest/gtest.h>

using namespace authforge::common::testing;
using authforge::common::model::Subject;

TEST(FakeSubjectResolverTest, ResolvesRegisteredMapping)
{
    FakeSubjectResolver resolver;
    resolver.addMapping(Subject("local:alice"), 42);

    std::optional<int32_t> result;
    resolver.resolve(Subject("local:alice"), [&](std::optional<int32_t> id) { result = id; });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST(FakeSubjectResolverTest, UnregisteredSubjectResolvesToNullopt)
{
    FakeSubjectResolver resolver;

    std::optional<int32_t> result = 99;
    resolver.resolve(Subject("local:unknown"), [&](std::optional<int32_t> id) { result = id; });

    EXPECT_FALSE(result.has_value());
}

TEST(FakeSubjectResolverTest, ClearRemovesMappings)
{
    FakeSubjectResolver resolver;
    resolver.addMapping(Subject("local:alice"), 1);
    resolver.clear();

    std::optional<int32_t> result = 5;
    resolver.resolve(Subject("local:alice"), [&](std::optional<int32_t> id) { result = id; });
    EXPECT_FALSE(result.has_value());
}
