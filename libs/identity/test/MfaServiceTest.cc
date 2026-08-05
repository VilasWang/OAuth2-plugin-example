// M2.5 identity completion (authforge-sdk-refactor): unit tests for
// authforge::identity::MfaService, exercised against a minimal in-memory
// fake IMfaRepository (no DB/no Drogon).

#include <authforge/common/testing/FakeClock.h>
#include <authforge/common/testing/FakeCryptoProvider.h>
#include <authforge/identity/IMfaRepository.h>
#include <authforge/identity/MfaService.h>
#include <authforge/identity/TotpUtils.h>

#include <gtest/gtest.h>

#include <unordered_map>

namespace
{

using namespace authforge::identity;
using authforge::common::testing::FakeClock;
using authforge::common::testing::FakeCryptoProvider;

class FakeMfaRepository : public IMfaRepository
{
  public:
    std::unordered_map<int32_t, MfaData> data;
    // Failure knobs (P1 coverage): let a test force setSecret/enable to
    // report failure so MfaService's error branches are reachable.
    bool failSetSecret = false;
    bool failEnable = false;

    void getMfaData(int32_t userId, MfaDataCallback &&cb) override
    {
        auto it = data.find(userId);
        cb(it == data.end() ? std::nullopt : std::make_optional(it->second));
    }

    void setSecret(int32_t userId, const std::string &secret, BoolCallback &&cb) override
    {
        if (failSetSecret)
        {
            cb(false);
            return;
        }
        data[userId].secret = secret;
        cb(true);
    }

    void enable(
      int32_t userId,
      const std::vector<std::string> &hashedBackupCodes,
      BoolCallback &&cb
    ) override
    {
        if (failEnable)
        {
            cb(false);
            return;
        }
        auto it = data.find(userId);
        if (it == data.end())
        {
            cb(false);
            return;
        }
        it->second.enabled = true;
        it->second.hashedBackupCodes = hashedBackupCodes;
        cb(true);
    }

    void disable(int32_t userId, BoolCallback &&cb) override
    {
        auto it = data.find(userId);
        if (it != data.end())
        {
            it->second.enabled = false;
            it->second.secret.clear();
            it->second.hashedBackupCodes.clear();
        }
        cb(true);
    }

    void setPendingBinding(
      int32_t userId,
      const std::string &clientId,
      const std::string &redirectUri,
      BoolCallback &&cb
    ) override
    {
        data[userId].pendingClientId = clientId;
        data[userId].pendingRedirectUri = redirectUri;
        cb(true);
    }

    void clearPendingBinding(int32_t userId, BoolCallback &&cb) override
    {
        auto it = data.find(userId);
        if (it != data.end())
        {
            it->second.pendingClientId.clear();
            it->second.pendingRedirectUri.clear();
        }
        cb(true);
    }
};

std::shared_ptr<MfaService> makeService(
  std::shared_ptr<FakeMfaRepository> repo,
  std::shared_ptr<FakeClock> clock
)
{
    auto crypto = std::make_shared<FakeCryptoProvider>();
    return std::make_shared<MfaService>(repo, crypto, clock);
}

}  // namespace

// ---------------------------------------------------------------------------
// Coverage additions (P1): null-dependency guards across every public
// method (MfaService.cc:27,55,107,121,147,160,177), the setSecret/enable
// failure branches, the empty-secret verifyAndEnable path, the unknown-user
// verifyLoginCode path, and the getPendingBinding unknown-user path. The
// null-guard tests construct MfaService directly with nullptr deps.
// ---------------------------------------------------------------------------

// setupSecret: null repo/crypto guard -> nullopt (MfaService.cc:27).
TEST(MfaServiceTest, SetupSecret_NullRepo_ReturnsNullopt)
{
    MfaService svc(nullptr, nullptr, nullptr);
    std::optional<MfaSetupResult> result = MfaSetupResult{};
    svc.setupSecret(42, "alice", [&](auto r) { result = std::move(r); });
    EXPECT_FALSE(result.has_value());
}

// setupSecret: setSecret returns ok=false -> nullopt (MfaService.cc:37-41).
TEST(MfaServiceTest, SetupSecret_SetSecretFails_ReturnsNullopt)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    repo->failSetSecret = true;
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);
    std::optional<MfaSetupResult> result = MfaSetupResult{};
    svc->setupSecret(42, "alice", [&](auto r) { result = std::move(r); });
    EXPECT_FALSE(result.has_value());
}

// verifyAndEnable: null mfaRepo/crypto/clock guard -> nullopt
// (MfaService.cc:55). Constructed with null clock.
TEST(MfaServiceTest, VerifyAndEnable_NullClock_ReturnsNullopt)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto crypto = std::make_shared<FakeCryptoProvider>();
    MfaService svc(repo, crypto, nullptr);
    std::optional<MfaEnableResult> result = MfaEnableResult{};
    svc.verifyAndEnable(42, "123456", [&](auto r) { result = std::move(r); });
    EXPECT_FALSE(result.has_value());
}

// verifyAndEnable: data present but secret empty -> nullopt
// (MfaService.cc:70).
TEST(MfaServiceTest, VerifyAndEnable_EmptySecret_ReturnsNullopt)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);
    repo->data[42].secret = "";  // present but empty
    repo->data[42].enabled = false;
    std::optional<MfaEnableResult> result = MfaEnableResult{};
    svc->verifyAndEnable(42, "123456", [&](auto r) { result = std::move(r); });
    EXPECT_FALSE(result.has_value());
}

// verifyAndEnable: enable returns ok=false -> nullopt (MfaService.cc:92-96).
TEST(MfaServiceTest, VerifyAndEnable_EnableFails_ReturnsNullopt)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    repo->failEnable = true;
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);

    // Set up a valid secret + correct code so verify passes, then enable
    // fails.
    std::optional<MfaSetupResult> setup;
    svc->setupSecret(42, "alice", [&](auto r) { setup = std::move(r); });
    ASSERT_TRUE(setup.has_value());
    std::string code = authforge::identity::totp::generateCode(setup->secret, clock->nowSeconds());

    std::optional<MfaEnableResult> result = MfaEnableResult{};
    svc->verifyAndEnable(42, code, [&](auto r) { result = std::move(r); });
    EXPECT_FALSE(result.has_value());
}

// disable: null repo guard -> false (MfaService.cc:107).
TEST(MfaServiceTest, Disable_NullRepo_ReturnsFalse)
{
    MfaService svc(nullptr, nullptr, nullptr);
    bool ok = true;
    svc.disable(42, [&](bool v) { ok = v; });
    EXPECT_FALSE(ok);
}

// verifyLoginCode: null mfaRepo/clock guard -> false (MfaService.cc:121).
TEST(MfaServiceTest, VerifyLoginCode_NullClock_ReturnsFalse)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto crypto = std::make_shared<FakeCryptoProvider>();
    MfaService svc(repo, crypto, nullptr);
    bool v = true;
    svc.verifyLoginCode(42, "123456", [&](bool r) { v = r; });
    EXPECT_FALSE(v);
}

// verifyLoginCode: unknown user (no data) -> false (MfaService.cc:130).
TEST(MfaServiceTest, VerifyLoginCode_UnknownUser_ReturnsFalse)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);
    bool v = true;
    svc->verifyLoginCode(999, "123456", [&](bool r) { v = r; });
    EXPECT_FALSE(v);
}

// verifyLoginCode: enabled but wrong code -> false (MfaService.cc:135).
TEST(MfaServiceTest, VerifyLoginCode_EnabledButWrongCode_ReturnsFalse)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);
    repo->data[42].secret = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
    repo->data[42].enabled = true;
    bool v = true;
    svc->verifyLoginCode(42, "000000", [&](bool r) { v = r; });
    EXPECT_FALSE(v);
}

// setPendingBinding: null repo guard -> false (MfaService.cc:147).
TEST(MfaServiceTest, SetPendingBinding_NullRepo_ReturnsFalse)
{
    MfaService svc(nullptr, nullptr, nullptr);
    bool ok = true;
    svc.setPendingBinding(42, "c", "u", [&](bool v) { ok = v; });
    EXPECT_FALSE(ok);
}

// getPendingBinding: null repo guard -> nullopt (MfaService.cc:160).
TEST(MfaServiceTest, GetPendingBinding_NullRepo_ReturnsNullopt)
{
    MfaService svc(nullptr, nullptr, nullptr);
    std::optional<std::pair<std::string, std::string>> binding = std::make_pair("x", "y");
    svc.getPendingBinding(42, [&](auto b) { binding = std::move(b); });
    EXPECT_FALSE(binding.has_value());
}

// getPendingBinding: unknown user (no data) -> nullopt (MfaService.cc:166).
TEST(MfaServiceTest, GetPendingBinding_UnknownUser_ReturnsNullopt)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);
    std::optional<std::pair<std::string, std::string>> binding = std::make_pair("x", "y");
    svc->getPendingBinding(999, [&](auto b) { binding = std::move(b); });
    EXPECT_FALSE(binding.has_value());
}

// clearPendingBinding: null repo guard -> false (MfaService.cc:177).
TEST(MfaServiceTest, ClearPendingBinding_NullRepo_ReturnsFalse)
{
    MfaService svc(nullptr, nullptr, nullptr);
    bool ok = true;
    svc.clearPendingBinding(42, [&](bool v) { ok = v; });
    EXPECT_FALSE(ok);
}

TEST(MfaServiceTest, SetupSecret_ReturnsSecretAndOtpUri)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);

    std::optional<MfaSetupResult> result;
    svc->setupSecret(42, "alice@example.com", [&](auto r) { result = std::move(r); });

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->secret.empty());
    EXPECT_NE(result->otpAuthUri.find("alice@example.com"), std::string::npos);
    EXPECT_EQ(repo->data[42].secret, result->secret);
}

TEST(MfaServiceTest, VerifyAndEnable_CorrectCode_EnablesAndReturnsBackupCodes)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);

    std::optional<MfaSetupResult> setupResult;
    svc->setupSecret(42, "alice", [&](auto r) { setupResult = std::move(r); });
    ASSERT_TRUE(setupResult.has_value());

    std::string code =
      authforge::identity::totp::generateCode(setupResult->secret, clock->nowSeconds());

    std::optional<MfaEnableResult> enableResult;
    svc->verifyAndEnable(42, code, [&](auto r) { enableResult = std::move(r); });

    ASSERT_TRUE(enableResult.has_value());
    EXPECT_EQ(enableResult->backupCodes.size(), 10u);
    EXPECT_TRUE(repo->data[42].enabled);
    EXPECT_EQ(repo->data[42].hashedBackupCodes.size(), 10u);
}

TEST(MfaServiceTest, VerifyAndEnable_WrongCode_ReturnsNulloptAndDoesNotEnable)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);

    svc->setupSecret(42, "alice", [](auto) {});

    std::optional<MfaEnableResult> enableResult;
    svc->verifyAndEnable(42, "000000", [&](auto r) { enableResult = std::move(r); });

    EXPECT_FALSE(enableResult.has_value());
    EXPECT_FALSE(repo->data[42].enabled);
}

TEST(MfaServiceTest, VerifyAndEnable_NoSecretSetup_ReturnsNullopt)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);

    std::optional<MfaEnableResult> enableResult;
    svc->verifyAndEnable(999, "123456", [&](auto r) { enableResult = std::move(r); });

    EXPECT_FALSE(enableResult.has_value());
}

TEST(MfaServiceTest, Disable_ClearsSecretAndEnabledFlag)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);

    svc->setupSecret(42, "alice", [](auto) {});
    repo->data[42].enabled = true;

    bool disabled = false;
    svc->disable(42, [&](bool ok) { disabled = ok; });

    EXPECT_TRUE(disabled);
    EXPECT_FALSE(repo->data[42].enabled);
    EXPECT_TRUE(repo->data[42].secret.empty());
}

TEST(MfaServiceTest, VerifyLoginCode_EnabledAndCorrect_ReturnsTrue)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);

    repo->data[42].secret = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
    repo->data[42].enabled = true;
    std::string code =
      authforge::identity::totp::generateCode(repo->data[42].secret, clock->nowSeconds());

    bool verified = false;
    svc->verifyLoginCode(42, code, [&](bool v) { verified = v; });
    EXPECT_TRUE(verified);
}

TEST(MfaServiceTest, VerifyLoginCode_NotEnabled_ReturnsFalse)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);

    repo->data[42].secret = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
    repo->data[42].enabled = false;
    std::string code =
      authforge::identity::totp::generateCode(repo->data[42].secret, clock->nowSeconds());

    bool verified = true;
    svc->verifyLoginCode(42, code, [&](bool v) { verified = v; });
    EXPECT_FALSE(verified);
}

TEST(MfaServiceTest, PendingBinding_SetGetClear_RoundTrips)
{
    auto repo = std::make_shared<FakeMfaRepository>();
    auto clock = std::make_shared<FakeClock>();
    auto svc = makeService(repo, clock);

    bool setOk = false;
    svc->setPendingBinding(42, "client-1", "https://example.test/cb", [&](bool ok) { setOk = ok; });
    EXPECT_TRUE(setOk);

    std::optional<std::pair<std::string, std::string>> binding;
    svc->getPendingBinding(42, [&](auto b) { binding = std::move(b); });
    ASSERT_TRUE(binding.has_value());
    EXPECT_EQ(binding->first, "client-1");
    EXPECT_EQ(binding->second, "https://example.test/cb");

    bool clearOk = false;
    svc->clearPendingBinding(42, [&](bool ok) { clearOk = ok; });
    EXPECT_TRUE(clearOk);

    std::optional<std::pair<std::string, std::string>> afterClear;
    svc->getPendingBinding(42, [&](auto b) { afterClear = std::move(b); });
    ASSERT_TRUE(afterClear.has_value());
    EXPECT_TRUE(afterClear->first.empty());
    EXPECT_TRUE(afterClear->second.empty());
}
