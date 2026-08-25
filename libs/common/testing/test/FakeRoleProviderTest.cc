// Task 15 (fulla-sdk-refactor, design.md §6/§8): pure gtest unit tests
// for FakeRoleProvider.

#include <fulla/common/testing/FakeRoleProvider.h>

#include <gtest/gtest.h>

using namespace fulla::common::testing;

TEST(FakeRoleProviderTest, ReturnsRegisteredRoles)
{
    FakeRoleProvider provider;
    provider.setRoles(1, {"admin", "editor"});

    std::vector<std::string> roles;
    provider.getRoles(1, [&](std::vector<std::string> r) { roles = std::move(r); });

    ASSERT_EQ(roles.size(), 2u);
    EXPECT_EQ(roles[0], "admin");
    EXPECT_EQ(roles[1], "editor");
}

TEST(FakeRoleProviderTest, UnregisteredUserReturnsEmptyRoles)
{
    FakeRoleProvider provider;

    std::vector<std::string> roles = {"should-be-cleared"};
    provider.getRoles(999, [&](std::vector<std::string> r) { roles = std::move(r); });

    EXPECT_TRUE(roles.empty());
}
