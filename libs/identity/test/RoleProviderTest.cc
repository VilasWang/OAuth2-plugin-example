// Task 19 (authforge-sdk-refactor, design.md §6): unit tests for
// authforge::identity::RoleProvider (implements
// authforge::common::ports::IRoleProvider).

#include <authforge/identity/RoleProvider.h>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <vector>

using authforge::identity::IRoleRepository;
using authforge::identity::RoleProvider;

namespace
{

class FakeRoleRepository : public IRoleRepository
{
  public:
    std::map<int64_t, std::vector<std::string>> roles;

    void getRoles(int64_t internalUserId, RolesCallback &&cb) override
    {
        auto it = roles.find(internalUserId);
        cb(it == roles.end() ? std::vector<std::string>{} : it->second);
    }
};

}  // namespace

TEST(RoleProviderTest, ForwardsToRepository)
{
    auto repo = std::make_shared<FakeRoleRepository>();
    repo->roles[42] = {"admin", "user"};
    RoleProvider provider(repo);

    std::vector<std::string> result;
    provider.getRoles(42, [&](std::vector<std::string> r) { result = r; });
    EXPECT_EQ(result, (std::vector<std::string>{"admin", "user"}));
}

TEST(RoleProviderTest, UnknownUserReturnsEmpty)
{
    auto repo = std::make_shared<FakeRoleRepository>();
    RoleProvider provider(repo);

    std::vector<std::string> result{"sentinel"};
    provider.getRoles(999, [&](std::vector<std::string> r) { result = r; });
    EXPECT_TRUE(result.empty());
}

TEST(RoleProviderTest, NullRepositoryReturnsEmptyInsteadOfCrashing)
{
    RoleProvider provider(nullptr);

    std::vector<std::string> result{"sentinel"};
    provider.getRoles(1, [&](std::vector<std::string> r) { result = r; });
    EXPECT_TRUE(result.empty());
}
