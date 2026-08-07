// Task 17 slice 3 (authforge-sdk-refactor): unit tests for the ported M1
// repository interfaces (authforge::oauth2::repository). These are
// interface/contract-shape tests -- minimal in-memory fake implementations
// exist only to prove the interfaces are usable (compile + basic
// polymorphic dispatch), NOT full contract tests (those remain
// tests/contract/*, which continue exercising the
// OAuth2Plugin-side implementations until a later slice switches them
// over). The one behavior worth actually asserting here is
// ITokenRepository::saveTokenPair's default body, since that's non-trivial
// logic (not just a pure-virtual declaration) carried over from the
// original.

#include <authforge/oauth2/model/Dto.h>
#include <authforge/oauth2/model/UserRef.h>
#include <authforge/oauth2/repository/IClientRepository.h>
#include <authforge/oauth2/repository/IConsentRepository.h>
#include <authforge/oauth2/repository/IGrantRepository.h>
#include <authforge/oauth2/repository/ITokenRepository.h>

#include <gtest/gtest.h>

#include <optional>
#include <unordered_map>

namespace
{

using namespace authforge::oauth2::model;
using namespace authforge::oauth2::repository;

// ---------------------------------------------------------------------
// Minimal fakes -- just enough to instantiate each interface and prove
// virtual dispatch works through a base-class pointer.
// ---------------------------------------------------------------------

class FakeClientRepository : public IClientRepository
{
  public:
    std::unordered_map<std::string, OAuth2Client> clients;

    void getClient(const std::string &clientId, ClientCallback &&cb) override
    {
        auto it = clients.find(clientId);
        cb(it == clients.end() ? std::nullopt : std::make_optional(it->second));
    }

    void validateClient(
      const std::string &clientId,
      const std::string & /*clientSecret*/,
      BoolCallback &&cb
    ) override
    {
        cb(clients.count(clientId) > 0);
    }
};

class FakeGrantRepository : public IGrantRepository
{
  public:
    void saveAuthCode(const OAuth2AuthCode & /*code*/, VoidCallback &&cb) override
    {
        cb();
    }

    void getAuthCode(const std::string & /*code*/, AuthCodeCallback &&cb) override
    {
        cb(std::nullopt);
    }

    void markAuthCodeUsed(const std::string & /*code*/, VoidCallback &&cb) override
    {
        cb();
    }

    void consumeAuthCode(
      const std::string & /*code*/,
      const std::string & /*redirectUri*/,
      AuthCodeCallback &&cb
    ) override
    {
        cb(std::nullopt);
    }

    void saveAuthorizationTransaction(
      const AuthorizationTransaction & /*transaction*/,
      BoolCallback &&cb
    ) override
    {
        cb(true);
    }

    void getAuthorizationTransaction(
      const std::string & /*transactionId*/,
      TransactionCallback &&cb
    ) override
    {
        cb(std::nullopt);
    }

    void deleteAuthorizationTransaction(
      const std::string & /*transactionId*/,
      VoidCallback &&cb
    ) override
    {
        cb();
    }

    void markTransactionConsumed(const std::string & /*transactionId*/, BoolCallback &&cb) override
    {
        cb(true);
    }

    void purgeExpired() override
    {
    }
};

// Records the order saveAccessToken/saveRefreshToken were invoked in, to
// verify ITokenRepository::saveTokenPair's default (sequential,
// access-then-refresh) body without overriding it.
class RecordingTokenRepository : public ITokenRepository
{
  public:
    std::vector<std::string> callOrder;

    void saveAccessToken(const OAuth2AccessToken & /*token*/, VoidCallback &&cb) override
    {
        callOrder.push_back("saveAccessToken");
        cb();
    }

    void getAccessToken(const std::string & /*token*/, AccessTokenCallback &&cb) override
    {
        cb(std::nullopt);
    }

    void saveRefreshToken(const OAuth2RefreshToken & /*token*/, VoidCallback &&cb) override
    {
        callOrder.push_back("saveRefreshToken");
        cb();
    }

    void getRefreshToken(const std::string & /*token*/, RefreshTokenCallback &&cb) override
    {
        cb(std::nullopt);
    }

    void revokeRefreshToken(const std::string & /*token*/, VoidCallback &&cb) override
    {
        cb();
    }

    void atomicRevokeRefreshToken(const std::string & /*token*/, RefreshTokenCallback &&cb) override
    {
        cb(std::nullopt);
    }

    void revokeTokenFamily(const std::string & /*familyId*/, VoidCallback &&cb) override
    {
        cb();
    }

    void introspectToken(const std::string & /*token*/, TokenIntrospectionCallback &&cb) override
    {
        cb(std::nullopt);
    }

    void incrementIntrospectCount(const std::string & /*token*/, VoidCallback &&cb) override
    {
        cb();
    }

    void revokeAccessToken(
      const std::string & /*token*/,
      const std::string & /*revokedBy*/,
      VoidCallback &&cb
    ) override
    {
        cb();
    }

    void purgeExpired() override
    {
    }

    bool supportsTransactions() const override
    {
        return false;
    }

    bool supportsCas() const override
    {
        return false;
    }
};

class FakeConsentRepository : public IConsentRepository
{
  public:
    void hasUserConsent(
      const UserRef & /*user*/,
      const std::string & /*clientId*/,
      const std::string & /*scope*/,
      BoolCallback &&cb
    ) override
    {
        cb(false);
    }

    void saveUserConsent(
      const UserRef & /*user*/,
      const std::string & /*clientId*/,
      const std::string & /*scope*/,
      BoolCallback &&cb
    ) override
    {
        cb(true);
    }

    void revokeUserConsent(
      const UserRef & /*user*/,
      const std::string & /*clientId*/,
      const std::string & /*scope*/,
      VoidCallback &&cb
    ) override
    {
        cb();
    }
};

TEST(IClientRepositoryTest, GetClient_ThroughBasePointer_DispatchesCorrectly)
{
    FakeClientRepository impl;
    OAuth2Client client;
    client.clientId = "abc";
    client.clientType = ClientType::PUBLIC;
    impl.clients["abc"] = client;

    IClientRepository &repo = impl;
    std::optional<OAuth2Client> result;
    repo.getClient("abc", [&](std::optional<OAuth2Client> c) { result = c; });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->clientId, "abc");
}

TEST(IClientRepositoryTest, ValidateClient_UnknownClient_ReturnsFalse)
{
    FakeClientRepository impl;
    IClientRepository &repo = impl;
    bool valid = true;
    repo.validateClient("nonexistent", "secret", [&](bool v) { valid = v; });
    EXPECT_FALSE(valid);
}

TEST(IGrantRepositoryTest, ConsumeAuthCode_ThroughBasePointer_Compiles)
{
    FakeGrantRepository impl;
    IGrantRepository &repo = impl;
    bool called = false;
    repo.consumeAuthCode("code", "https://example.com/cb", [&](std::optional<OAuth2AuthCode> c) {
        called = true;
        EXPECT_FALSE(c.has_value());
    });
    EXPECT_TRUE(called);
}

TEST(ITokenRepositoryTest, SaveTokenPair_DefaultBody_SavesAccessThenRefreshSequentially)
{
    RecordingTokenRepository impl;
    ITokenRepository &repo = impl;  // exercise the default body via base ptr

    OAuth2AccessToken at;
    OAuth2RefreshToken rt;
    bool completed = false;

    // SaveResultCallback: the default body (sequential Memory/Redis-style
    // save) always reports ok == true.
    repo.saveTokenPair(at, rt, [&](bool ok) { completed = ok; });

    ASSERT_TRUE(completed);
    ASSERT_EQ(impl.callOrder.size(), 2u);
    EXPECT_EQ(impl.callOrder[0], "saveAccessToken");
    EXPECT_EQ(impl.callOrder[1], "saveRefreshToken");
}

TEST(IConsentRepositoryTest, SaveThenHasConsent_ThroughBasePointer_Compiles)
{
    FakeConsentRepository impl;
    IConsentRepository &repo = impl;
    UserRef user{42};

    bool saved = false;
    repo.saveUserConsent(user, "client-1", "openid", [&](bool ok) { saved = ok; });
    EXPECT_TRUE(saved);
}

}  // namespace
