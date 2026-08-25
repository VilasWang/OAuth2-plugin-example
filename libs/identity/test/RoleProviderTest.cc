// Task 19 (fulla-sdk-refactor, design.md §6): unit tests for
// fulla::identity::RoleProvider (implements
// fulla::common::ports::IRoleProvider).

#include <fulla/identity/RoleProvider.h>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <vector>

using fulla::identity::IRoleRepository;
using fulla::identity::RoleProvider;

namespace
{

class FakeRoleRepository : public IRoleRepository
{
  public:
    std::map<int32_t, std::vector<std::string>> roles;

    void getRoles(int32_t internalUserId, RolesCallback &&cb) override
    {
        auto it = roles.find(internalUserId);
        cb(it == roles.end() ? std::vector<std::string>{} : it->second);
    }

    void getRoles(const std::string &subject, RolesCallback &&cb) override
    {
        // Test fake: numeric subject -> internal id; otherwise empty.
        try
        {
            cb(roles.at(std::stoi(subject)));
        }
        catch (...)
        {
            cb({});
        }
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
