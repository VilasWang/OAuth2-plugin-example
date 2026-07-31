// Task 15 (authforge-sdk-refactor, design.md §6/§8): pure gtest unit tests
// for FakeUserInfoProvider.

#include <authforge/common/testing/FakeUserInfoProvider.h>

#include <gtest/gtest.h>

using namespace authforge::common::testing;

TEST(FakeUserInfoProviderTest, ReturnsRegisteredClaims)
{
    FakeUserInfoProvider provider;
    Json::Value claims;
    claims["sub"] = "alice";
    claims["email"] = "alice@example.com";
    provider.setUserInfo(1, claims);

    std::optional<Json::Value> result;
    provider.getUserInfo(1, [&](std::optional<Json::Value> c) { result = std::move(c); });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)["sub"].asString(), "alice");
    EXPECT_EQ((*result)["email"].asString(), "alice@example.com");
}

TEST(FakeUserInfoProviderTest, UnregisteredUserReturnsNullopt)
{
    FakeUserInfoProvider provider;

    std::optional<Json::Value> result = Json::Value(Json::objectValue);
    provider.getUserInfo(999, [&](std::optional<Json::Value> c) { result = std::move(c); });

    EXPECT_FALSE(result.has_value());
}
