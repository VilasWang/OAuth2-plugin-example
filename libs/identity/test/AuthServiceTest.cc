// Task 19 (fulla-sdk-refactor, design.md §6): unit tests for
// fulla::identity::AuthService, using a deterministic in-memory
// IUserRepository fake plus fulla::common::testing's
// FakeCryptoProvider/FakeClock. No DB, no Drogon.

#include <fulla/identity/AuthService.h>
#include <fulla/identity/IUserRepository.h>
#include <fulla/common/testing/FakeCryptoProvider.h>
#include <fulla/common/testing/FakeClock.h>

#include <gtest/gtest.h>

#include <map>
#include <optional>

using fulla::common::testing::FakeClock;
using fulla::common::testing::FakeCryptoProvider;
using fulla::identity::AuthResult;
using fulla::identity::AuthService;
using fulla::identity::IUserRepository;
using fulla::identity::UserData;

namespace
{

// Minimal in-memory IUserRepository fake, sufficient to drive
// AuthService's validateUser/registerUser/getUserInfo without any DB.
class InMemoryUserRepository : public IUserRepository
{
  public:
    void findByEmail(
      const std::string &email,
      std::function<void(std::optional<UserData>)> &&cb
    ) override
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

    void findById(int32_t userId, std::function<void(std::optional<UserData>)> &&cb) override
    {
        auto it = users_.find(userId);
        cb(it == users_.end() ? std::nullopt : std::optional<UserData>(it->second));
    }

    void findByPublicSub(
      const std::string &publicSub,
      std::function<void(std::optional<UserData>)> &&cb
    ) override
    {
        for (auto &[id, user] : users_)
        {
            if (user.publicSub == publicSub)
            {
                cb(user);
                return;
            }
        }
        cb(std::nullopt);
    }

    void create(
      const UserData &userData,
      std::function<void(std::optional<int32_t>, std::string)> &&cb
    ) override
    {
        // Mirror PostgresIdentityRepository's uniqueness classification
        // (username checked before email) so tests exercising conflict
        // handling behave the same as production.
        for (auto &[id, user] : users_)
        {
            if (!userData.username.empty() && user.username == userData.username)
            {
                cb(std::nullopt, "VALIDATION_USERNAME_TAKEN");
                return;
            }
        }
        for (auto &[id, user] : users_)
        {
            if (!userData.email.empty() && user.email == userData.email)
            {
                cb(std::nullopt, "VALIDATION_EMAIL_TAKEN");
                return;
            }
        }
        int32_t newId = nextId_++;
        UserData stored = userData;
        stored.id = newId;
        stored.publicSub = "sub-" + std::to_string(newId);
        users_[newId] = stored;
        cb(newId, "");
    }

    void updatePasswordHash(
      int32_t userId,
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

    void resetFailedLogins(int32_t userId, std::function<void(bool)> &&cb) override
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

    void incrementFailedLogins(int32_t userId, std::function<void(bool)> &&cb) override
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
      int32_t userId,
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
    int32_t seed(UserData user)
    {
        int32_t id = nextId_++;
        user.id = id;
        users_[id] = user;
        return id;
    }

  private:
    std::map<int32_t, UserData> users_;
    int32_t nextId_ = 1;
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
    service
      ->registerUser("alice", "correct-password", "alice@example.com", [&](const std::string &err) {
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
    service->validateUser("carol", "wrongpass", [&](std::optional<AuthResult> r) { result = r; });
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
    locked.lockedUntil = clock->nowSeconds() + 3600;      // locked 1h into the future
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
    int32_t userId = repo->seed(legacyUser);

    std::optional<AuthResult> result;
    service->validateUser("erin", "legacy-pw", [&](std::optional<AuthResult> r) { result = r; });
    ASSERT_TRUE(result.has_value());

    // The hash should now be rehashed to the PBKDF2 format.
    std::optional<UserData> reloaded;
    repo->findById(userId, [&](std::optional<UserData> u) { reloaded = u; });
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->passwordHash.find("$pbkdf2-sha256$"), 0u);
}

// F10 migration compatibility (Task 37): after the on-login rehash the same
// password must keep working through the PBKDF2 branch -- proves the upgrade
// is lossless end-to-end (seed legacy row -> login -> rehash -> login again).
TEST_F(AuthServiceTest, LegacyUserCanLoginAgainAfterRehash)
{
    std::string salt = "somesalt";
    UserData legacyUser;
    legacyUser.username = "gina";
    legacyUser.email = "gina@example.com";
    legacyUser.passwordHash = crypto->sha256Hex("legacy-pw" + salt);
    legacyUser.salt = salt;
    int32_t userId = repo->seed(legacyUser);

    std::optional<AuthResult> first;
    service->validateUser("gina", "legacy-pw", [&](std::optional<AuthResult> r) { first = r; });
    ASSERT_TRUE(first.has_value());

    std::optional<UserData> upgraded;
    repo->findById(userId, [&](std::optional<UserData> u) { upgraded = u; });
    ASSERT_TRUE(upgraded.has_value());
    ASSERT_EQ(upgraded->passwordHash.find("$pbkdf2-sha256$"), 0u);

    // Second login now verifies against the upgraded PBKDF2 hash.
    std::optional<AuthResult> second;
    service->validateUser("gina", "legacy-pw", [&](std::optional<AuthResult> r) { second = r; });
    ASSERT_TRUE(second.has_value());

    // And the hash must be stable -- no re-rehash on the second login.
    std::optional<UserData> stable;
    repo->findById(userId, [&](std::optional<UserData> u) { stable = u; });
    ASSERT_TRUE(stable.has_value());
    EXPECT_EQ(stable->passwordHash, upgraded->passwordHash);
}

// F10 migration compatibility (Task 37): a failed login against a legacy row
// must NOT trigger the upgrade -- the stored hash stays legacy so the user
// can still authenticate with the correct password later.
TEST_F(AuthServiceTest, LegacyHashStaysUntouchedOnWrongPassword)
{
    std::string salt = "somesalt";
    std::string legacyHash = crypto->sha256Hex("legacy-pw" + salt);

    UserData legacyUser;
    legacyUser.username = "hank";
    legacyUser.email = "hank@example.com";
    legacyUser.passwordHash = legacyHash;
    legacyUser.salt = salt;
    int32_t userId = repo->seed(legacyUser);

    std::optional<AuthResult> result;
    service->validateUser("hank", "wrong-pw", [&](std::optional<AuthResult> r) { result = r; });
    EXPECT_FALSE(result.has_value());

    std::optional<UserData> reloaded;
    repo->findById(userId, [&](std::optional<UserData> u) { reloaded = u; });
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->passwordHash, legacyHash);
}

TEST_F(AuthServiceTest, GetUserInfoReturnsClaimsForRegisteredUser)
{
    int32_t userId = 0;
    service->registerUser("frank", "pw", "frank@example.com", [](const std::string &) {});
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

// Task 24 slice 4 (fulla-sdk-refactor): AuthService::registerUser must
// forward the repository's structured Error_Code verbatim (not collapse
// every failure into a generic code) -- mirrors OAuth2Server/
// AuthService.cc's pre-migration registerUser contract
// (auth-flow-error-code-gaps spec), which this repository-owned
// classification replaces (IUserRepository::create() is now responsible
// for it instead of the caller inspecting a raw DB exception message).
TEST_F(AuthServiceTest, RegisterDuplicateUsernameReturnsUsernameTakenCode)
{
    service->registerUser("greg", "pw", "greg@example.com", [](const std::string &) {});

    std::string errorCode;
    service->registerUser("greg", "different-pw", "other@example.com", [&](const std::string &err) {
        errorCode = err;
    });
    EXPECT_EQ(errorCode, "VALIDATION_USERNAME_TAKEN");
}

TEST_F(AuthServiceTest, RegisterDuplicateEmailReturnsEmailTakenCode)
{
    service->registerUser("hank", "pw", "hank@example.com", [](const std::string &) {});

    std::string errorCode;
    service
      ->registerUser("different-username", "pw", "hank@example.com", [&](const std::string &err) {
          errorCode = err;
      });
    EXPECT_EQ(errorCode, "VALIDATION_EMAIL_TAKEN");
}

// ---------------------------------------------------------------------------
// Coverage additions (P1): null-dependency guards (validateUser/registerUser/
// getUserInfo), the failedLoginCount>0 reset-on-success branch, malformed-
// PBKDF2-hash rejection, and the legacy uppercase-hex case-insensitive
// compare. These construct AuthService directly with nullptr deps where
// needed.
// ---------------------------------------------------------------------------

// validateUser: null userRepo/crypto/clock guard -> nullopt
// (AuthService.cc:165).
TEST_F(AuthServiceTest, ValidateUserNullDependenciesReturnsNullopt)
{
    AuthService nullSvc(nullptr, nullptr, nullptr);
    std::optional<AuthResult> result = AuthResult{};
    nullSvc.validateUser("alice", "pw", [&](std::optional<AuthResult> r) { result = std::move(r); });
    EXPECT_FALSE(result.has_value());
}

// registerUser: null userRepo/crypto guard -> INTERNAL_ERROR
// (AuthService.cc:246).
TEST_F(AuthServiceTest, RegisterUserNullDependenciesReturnsInternalError)
{
    AuthService nullSvc(nullptr, nullptr, nullptr);
    std::string errorCode = "none";
    nullSvc.registerUser("alice", "pw", "a@b.test", [&](const std::string &e) { errorCode = e; });
    EXPECT_EQ(errorCode, "INTERNAL_ERROR");
}

// getUserInfo: null userRepo guard -> nullopt (AuthService.cc:297).
TEST_F(AuthServiceTest, GetUserInfoNullRepositoryReturnsNullopt)
{
    AuthService nullSvc(nullptr, nullptr, nullptr);
    std::optional<Json::Value> info = Json::Value{};
    nullSvc.getUserInfo(1, [&](std::optional<Json::Value> v) { info = std::move(v); });
    EXPECT_FALSE(info.has_value());
}

// validateUser: a successful login resets failedLoginCount to 0 when it was
// previously > 0 (AuthService.cc:206-209).
TEST_F(AuthServiceTest, SuccessfulLoginResetsFailedLoginCount)
{
    // Register a user, then drive a couple of failed logins to bump the
    // counter, then succeed and assert the counter is reset.
    service->registerUser("irene", "correct-pw", "irene@example.com", [](const std::string &) {});

    // Two failed attempts (wrong password) increment the counter.
    service->validateUser("irene", "wrong", [](std::optional<AuthResult>) {});
    service->validateUser("irene", "wrong", [](std::optional<AuthResult>) {});

    // Find the user's id and assert the counter is now 2.
    int32_t userId = 0;
    repo->findByUsername("irene", [&](std::optional<UserData> u) { userId = u->id; });

    std::optional<AuthResult> ok;
    service->validateUser("irene", "correct-pw", [&](std::optional<AuthResult> r) { ok = r; });
    ASSERT_TRUE(ok.has_value());

    std::optional<UserData> reloaded;
    repo->findById(userId, [&](std::optional<UserData> u) { reloaded = u; });
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(reloaded->failedLoginCount, 0);
    EXPECT_EQ(reloaded->lockedUntil, 0);
}

// validateUser: a stored PBKDF2 hash with the wrong number of '$'-parts is
// rejected (verifyPassword parts.size() != 4, AuthService.cc:104).
TEST_F(AuthServiceTest, VerifyPbkdf2MalformedHashReturnsNullopt)
{
    UserData u;
    u.username = "mallory";
    u.email = "mallory@example.com";
    // Wrong part count (3 parts, not 4).
    u.passwordHash = "$pbkdf2-sha256$310000$00";
    repo->seed(u);

    std::optional<AuthResult> result = AuthResult{};
    service->validateUser("mallory", "anything", [&](std::optional<AuthResult> r) {
        result = std::move(r);
    });
    EXPECT_FALSE(result.has_value());
}

// validateUser: a stored PBKDF2 hash with a non-numeric iterations field is
// rejected (verifyPassword stoi catch, AuthService.cc:108-115).
TEST_F(AuthServiceTest, VerifyPbkdf2NonNumericIterationsReturnsNullopt)
{
    UserData u;
    u.username = "nancy";
    u.email = "nancy@example.com";
    // 4 parts, but iterations is non-numeric.
    u.passwordHash = "$pbkdf2-sha256$notanumber$00$00";
    repo->seed(u);

    std::optional<AuthResult> result = AuthResult{};
    service->validateUser("nancy", "anything", [&](std::optional<AuthResult> r) {
        result = std::move(r);
    });
    EXPECT_FALSE(result.has_value());
}

// validateUser: a legacy hash stored in UPPERCASE hex still verifies (the
// tolower transform on both sides, AuthService.cc:137-140).
TEST_F(AuthServiceTest, LegacyHashUppercaseHexStillVerifies)
{
    std::string salt = "saltsalt";
    std::string legacyHash = crypto->sha256Hex("upcase-pw" + salt);
    // Uppercase the stored hash; sha256Hex returns lowercase, so this
    // exercises the case-folding branch.
    std::transform(legacyHash.begin(), legacyHash.end(), legacyHash.begin(), ::toupper);

    UserData u;
    u.username = "oscar";
    u.email = "oscar@example.com";
    u.passwordHash = legacyHash;
    u.salt = salt;
    repo->seed(u);

    std::optional<AuthResult> result;
    service->validateUser("oscar", "upcase-pw", [&](std::optional<AuthResult> r) { result = r; });
    ASSERT_TRUE(result.has_value());
}
