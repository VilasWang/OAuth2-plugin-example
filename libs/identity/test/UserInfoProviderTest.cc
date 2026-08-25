// Task 19 (fulla-sdk-refactor, design.md §6): unit tests for
// fulla::identity::UserInfoProvider (implements
// fulla::common::ports::IUserInfoProvider).

#include <fulla/identity/UserInfoProvider.h>

#include <gtest/gtest.h>

#include <map>
#include <memory>

using fulla::identity::IUserRepository;
using fulla::identity::UserData;
using fulla::identity::UserInfoProvider;

namespace
{

class FakeUserRepository : public IUserRepository
{
  public:
    std::map<int32_t, Json::Value> infos;

    void findByEmail(
      const std::string &,
      std::function<void(std::optional<UserData>)> &&cb
    ) override
    {
        cb(std::nullopt);
    }

    void findByUsername(
      const std::string &,
      std::function<void(std::optional<UserData>)> &&cb
    ) override
    {
        cb(std::nullopt);
    }

    void findById(int32_t, std::function<void(std::optional<UserData>)> &&cb) override
    {
        cb(std::nullopt);
    }

    void findByPublicSub(
      const std::string &,
      std::function<void(std::optional<UserData>)> &&cb
    ) override
    {
        cb(std::nullopt);
    }

    void create(
      const UserData &,
      std::function<void(std::optional<int32_t>, std::string)> &&cb
    ) override
    {
        cb(std::nullopt, "INTERNAL_ERROR");
    }

    void updatePasswordHash(int32_t, const std::string &, std::function<void(bool)> &&cb) override
    {
        cb(false);
    }

    void resetFailedLogins(int32_t, std::function<void(bool)> &&cb) override
    {
        cb(false);
    }

    void incrementFailedLogins(int32_t, std::function<void(bool)> &&cb) override
    {
        cb(false);
    }

    void getUserInfoWithRoles(
      int32_t userId,
      std::function<void(std::optional<Json::Value>)> &&cb
    ) override
    {
        auto it = infos.find(userId);
        cb(it == infos.end() ? std::nullopt : std::optional<Json::Value>(it->second));
    }
};

}  // namespace

TEST(UserInfoProviderTest, ForwardsToRepository)
{
    auto repo = std::make_shared<FakeUserRepository>();
    Json::Value claims;
    claims["sub"] = "sub-1";
    claims["email"] = "a@example.com";
    repo->infos[1] = claims;

    UserInfoProvider provider(repo);

    std::optional<Json::Value> result;
    provider.getUserInfo(1, [&](std::optional<Json::Value> j) { result = j; });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)["email"].asString(), "a@example.com");
}

TEST(UserInfoProviderTest, UnknownUserReturnsNullopt)
{
    auto repo = std::make_shared<FakeUserRepository>();
    UserInfoProvider provider(repo);

    std::optional<Json::Value> result;
    provider.getUserInfo(999, [&](std::optional<Json::Value> j) { result = j; });
    EXPECT_FALSE(result.has_value());
}

TEST(UserInfoProviderTest, NullRepositoryReturnsNulloptInsteadOfCrashing)
{
    UserInfoProvider provider(nullptr);

    std::optional<Json::Value> result;
    provider.getUserInfo(1, [&](std::optional<Json::Value> j) { result = j; });
    EXPECT_FALSE(result.has_value());
}
