// Task 19 (authforge-sdk-refactor, design.md §6): unit tests for
// authforge::identity::AuthService, using a deterministic in-memory
// IUserRepository fake plus authforge::common::testing's
// FakeCryptoProvider/FakeClock. No DB, no Drogon.

#include <authforge/identity/AuthService.h>
#include <authforge/identity/IUserRepository.h>
#include <authforge/common/testing/FakeCryptoProvider.h>
#include <authforge/common/testing/FakeClock.h>

#include <gtest/gtest.h>

#include <map>
#include <optional>

using authforge::common::testing::FakeClock;
using authforge::common::testing::FakeCryptoProvider;
using authforge::identity::AuthResult;
using authforge::identity::AuthService;
using authforge::identity::IUserRepository;
using authforge::identity::UserData;

namespace
{

// Minimal in-memory IUserRepository fake, sufficient to drive
// AuthService's validateUser/registerUser/getUserInfo without any DB.
class InMemoryUserRepository : public IUserRepository
{
  public:
    void findByEmail(const std::string &email, std::function<void(std::optional<UserData>)> &&cb)
      override
    {
        for (auto &[id, user] : users_)
        {
            if (user.email == email)
            {
                cb(user);
                return;
            }
        }
        cb(std::nullopt);
    }

    void findByUsername(
      const std::string &username,
      std::function<void(std::optional<UserData>)> &&cb
    ) override
    {
        for (auto &[id, user] : users_)
        {
            if (user.username == username)
            {
                cb(user);
                return;
            }
        }
        cb(std::nullopt);
    }

    void findById(int64_t userId, std::function<void(std::optional<UserData>)> &&cb) override
    {
        auto it = users_.find(userId);
        cb(it == users_.end() ? std::nullopt : std::optional<UserData>(it->second));
    }

    void create(const UserData &userData, std::function<void(std::optional<int64_t>)> &&cb)
      override
    {
        int64_t newId = nextId_++;
        UserData stored = userData;
        stored.id = newId;
        stored.publicSub = "sub-" + std::to_string(newId);
        users_[newId] = stored;
        cb(newId);
    }

    void updatePasswordHash(
      int64_t userId,
      const std::string &newHash,
      std::function<void(bool)> &&cb
    ) override
    {
        auto it = users_.find(userId);
        if (it == users_.end())
        {
            cb(false);
            return;
        }
        it->second.passwordHash = newHash;
        cb(true);
    }

    void resetFailedLogins(int64_t userId, std::function<void(bool)> &&cb) override
    {
        auto it = users_.find(userId);
        if (it == users_.end())
        {
            cb(false);
            return;
        }
        it->second.failedLoginCount = 0;
        it->second.lockedUntil = 0;
        cb(true);
    }

    void incrementFailedLogins(int64_t userId, std::function<void(bool)> &&cb) override
    {
        auto it = users_.find(userId);
        if (it == users_.end())
        {
            cb(false);
            return;
        }
        it->second.failedLoginCount++;
        cb(true);
    }

    void getUserInfoWithRoles(
      int64_t userId,
      std::function<void(std::optional<Json::Value>)> &&cb
    ) override
    {
        auto it = users_.find(userId);
        if (it == users_.end())
        {
            cb(std::nullopt);
            return;
        }
        Json::Value json;
        json["sub"] = it->second.publicSub;
        json["email"] = it->second.email;
        json["roles"] = Json::Value(Json::arrayValue);
        cb(json);
    }

    // Test helper: directly seed a user record (bypassing create()'s
    // auto-generated publicSub) for tests that need a pre-existing user
    // with a specific password hash/lockout state.
    int64_t seed(UserData user)
    {
        int64_t id = nextId_++;
        user.id = id;
        users_[id] = user;
        return id;
    }

  private:
    std::map<int64_t, UserData> users_;
    int64_t nextId_ = 1;
};

class AuthServiceTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        repo = std::make_shared<InMemoryUserRepository>();
        crypto = std::make_shared<FakeCryptoProvider>();
        clock = std::make_shared<FakeClock>();
        service = std::make_unique<AuthService>(repo, crypto, clock);
    }

    std::shared_ptr<InMemoryUserRepository> repo;
    std::shared_ptr<FakeCryptoProvider> crypto;
    std::shared_ptr<FakeClock> clock;
    std::unique_ptr<AuthService> service;
};

}  // namespace

TEST_F(AuthServiceTest, RegisterThenValidateSucceeds)
{
    std::string errorCode;
    service->registerUser("alice", "correct-password", "alice@example.com", [&](const std::string &err) {
        errorCode = err;
    });
    ASSERT_EQ(errorCode, "");

    std::optional<AuthResult> result;
    service->validateUser("alice", "correct-password", [&](std::optional<AuthResult> r) {
        result = r;
    });
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->publicSub.empty());
}

TEST_F(AuthServiceTest, ValidateUserByEmail)
{
    service->registerUser("bob", "s3cret", "bob@example.com", [](const std::string &) {});

    std::optional<AuthResult> result;
    service->validateUser("bob@example.com", "s3cret", [&](std::optional<AuthResult> r) {
        result = r;
    });
    ASSERT_TRUE(result.has_value());
}

TEST_F(AuthServiceTest, WrongPasswordFails)
{
    service->registerUser("carol", "rightpass", "carol@example.com", [](const std::string &) {});

    std::optional<AuthResult> result;
    service->validateUser("carol", "wrongpass", [&](std::optional<AuthResult> r) {
        result = r;
    });
    EXPECT_FALSE(result.has_value());
}

TEST_F(AuthServiceTest, UnknownIdentifierFails)
{
    std::optional<AuthResult> result;
    service->validateUser("nobody", "whatever", [&](std::optional<AuthResult> r) { result = r; });
    EXPECT_FALSE(result.has_value());
}

TEST_F(AuthServiceTest, LockedAccountRejectsEvenCorrectPassword)
{
    UserData locked;
    locked.username = "dave";
    locked.email = "dave@example.com";
    locked.passwordHash = "$pbkdf2-sha256$310000$00$00";  // irrelevant, never reached
    locked.lockedUntil = clock->nowSeconds() + 3600;       // locked 1h into the future
    repo->seed(locked);

    std::optional<AuthResult> result;
    service->validateUser("dave", "anything", [&](std::optional<AuthResult> r) { result = r; });
    EXPECT_FALSE(result.has_value());
}

TEST_F(AuthServiceTest, LegacyHashVerifiesAndGetsUpgraded)
{
    // Build a legacy SHA-256(password + salt) hex hash the same way the
    // pre-migration PasswordHasher::verify() legacy branch expects.
    std::string salt = "somesalt";
    std::string legacyHash = crypto->sha256Hex("legacy-pw" + salt);

    UserData legacyUser;
    legacyUser.username = "erin";
    legacyUser.email = "erin@example.com";
    legacyUser.passwordHash = legacyHash;
    legacyUser.salt = salt;
    int64_t userId = repo->seed(legacyUser);

    std::optional<AuthResult> result;
    service->validateUser("erin", "legacy-pw", [&](std::optional<AuthResult> r) { result = r; });
    ASSERT_TRUE(result.has_value());

    // The hash should now be rehashed to the PBKDF2 format.
    std::optional<UserData> reloaded;
    repo->findById(userId, [&](std::optional<UserData> u) { reloaded = u; });
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->passwordHash.find("$pbkdf2-sha256$"), 0u);
}

TEST_F(AuthServiceTest, GetUserInfoReturnsClaimsForRegisteredUser)
{
    int64_t userId = 0;
    service->registerUser(
      "frank",
      "pw",
      "frank@example.com",
      [](const std::string &) {}
    );
    repo->findByUsername("frank", [&](std::optional<UserData> u) {
        if (u)
            userId = u->id;
    });
    ASSERT_NE(userId, 0);

    std::optional<Json::Value> info;
    service->getUserInfo(userId, [&](std::optional<Json::Value> j) { info = j; });
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ((*info)["email"].asString(), "frank@example.com");
}

TEST_F(AuthServiceTest, GetUserInfoReturnsNulloptForUnknownUser)
{
    std::optional<Json::Value> info;
    service->getUserInfo(99999, [&](std::optional<Json::Value> j) { info = j; });
    EXPECT_FALSE(info.has_value());
}
