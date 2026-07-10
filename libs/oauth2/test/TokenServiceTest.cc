// Task 17 remainder (authforge-sdk-refactor): unit tests for the new
// Domain-layer authforge::oauth2::protocol::TokenService, exercised
// against minimal in-memory fake repository/port implementations (no
// Drogon, no OAuth2Plugin). Mirrors the shape/coverage of
// Property4_TokenFlowBaselineTest.cc (which pins the OLD
// OAuth2Plugin-side oauth2::TokenService's behavior) so this NEW class's
// behavior can be visually diffed against that baseline for parity.

#include <authforge/common/testing/FakeCryptoProvider.h>
#include <authforge/oauth2/protocol/TokenService.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <unordered_map>

namespace
{

using namespace authforge::oauth2::model;
using namespace authforge::oauth2::repository;
using authforge::oauth2::protocol::TokenService;

class FakeClientRepo : public IClientRepository
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
      const std::string &clientSecret,
      BoolCallback &&cb
    ) override
    {
        auto it = clients.find(clientId);
        if (it == clients.end())
        {
            cb(false);
            return;
        }
        cb(it->second.clientSecretHash == clientSecret);
    }
};

class FakeGrantRepo : public IGrantRepository
{
  public:
    std::unordered_map<std::string, OAuth2AuthCode> codes;

    void saveAuthCode(const OAuth2AuthCode &code, VoidCallback &&cb) override
    {
        codes[code.code] = code;
        cb();
    }
    void getAuthCode(const std::string &code, AuthCodeCallback &&cb) override
    {
        auto it = codes.find(code);
        cb(it == codes.end() ? std::nullopt : std::make_optional(it->second));
    }
    void markAuthCodeUsed(const std::string &code, VoidCallback &&cb) override
    {
        auto it = codes.find(code);
        if (it != codes.end())
            it->second.used = true;
        cb();
    }
    void consumeAuthCode(
      const std::string &code,
      const std::string &redirectUri,
      AuthCodeCallback &&cb
    ) override
    {
        auto it = codes.find(code);
        if (it == codes.end() || it->second.used || it->second.redirectUri != redirectUri)
        {
            cb(std::nullopt);
            return;
        }
        auto result = it->second;
        it->second.used = true;
        cb(result);
    }
    void saveAuthorizationTransaction(const AuthorizationTransaction &, BoolCallback &&cb) override
    {
        cb(true);
    }
    void getAuthorizationTransaction(const std::string &, TransactionCallback &&cb) override
    {
        cb(std::nullopt);
    }
    void deleteAuthorizationTransaction(const std::string &, VoidCallback &&cb) override
    {
        cb();
    }
    void markTransactionConsumed(const std::string &, BoolCallback &&cb) override
    {
        cb(true);
    }
    void purgeExpired() override
    {
    }
};

class FakeTokenRepo : public ITokenRepository
{
  public:
    std::unordered_map<std::string, OAuth2AccessToken> accessTokens;
    std::unordered_map<std::string, OAuth2RefreshToken> refreshTokens;

    void saveAccessToken(const OAuth2AccessToken &token, VoidCallback &&cb) override
    {
        accessTokens[token.token] = token;
        cb();
    }
    void getAccessToken(const std::string &token, AccessTokenCallback &&cb) override
    {
        auto it = accessTokens.find(token);
        cb(it == accessTokens.end() ? std::nullopt : std::make_optional(it->second));
    }
    void saveRefreshToken(const OAuth2RefreshToken &token, VoidCallback &&cb) override
    {
        refreshTokens[token.token] = token;
        cb();
    }
    void getRefreshToken(const std::string &token, RefreshTokenCallback &&cb) override
    {
        auto it = refreshTokens.find(token);
        cb(it == refreshTokens.end() ? std::nullopt : std::make_optional(it->second));
    }
    void revokeRefreshToken(const std::string &token, VoidCallback &&cb) override
    {
        auto it = refreshTokens.find(token);
        if (it != refreshTokens.end())
            it->second.revoked = true;
        cb();
    }
    void atomicRevokeRefreshToken(const std::string &token, RefreshTokenCallback &&cb) override
    {
        auto it = refreshTokens.find(token);
        if (it == refreshTokens.end() || it->second.revoked)
        {
            cb(std::nullopt);
            return;
        }
        auto result = it->second;
        it->second.revoked = true;
        cb(result);
    }
    void revokeTokenFamily(const std::string &familyId, VoidCallback &&cb) override
    {
        for (auto &[key, rt] : refreshTokens)
        {
            if (rt.familyId == familyId)
                rt.revoked = true;
        }
        for (auto &[key, at] : accessTokens)
        {
            (void)key;
            (void)at;
        }
        cb();
    }
    void introspectToken(const std::string &token, TokenIntrospectionCallback &&cb) override
    {
        auto it = accessTokens.find(token);
        if (it == accessTokens.end())
        {
            TokenIntrospection inactive;
            inactive.active = false;
            cb(inactive);
            return;
        }
        TokenIntrospection intro;
        intro.active = !it->second.revoked;
        intro.clientId = it->second.clientId;
        intro.sub = it->second.userId;
        intro.scope = it->second.scope;
        intro.exp = it->second.expiresAt;
        cb(intro);
    }
    void incrementIntrospectCount(const std::string &, VoidCallback &&cb) override
    {
        cb();
    }
    void revokeAccessToken(const std::string &token, const std::string &, VoidCallback &&cb) override
    {
        auto it = accessTokens.find(token);
        if (it != accessTokens.end())
            it->second.revoked = true;
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
        return true;
    }
};

std::shared_ptr<FakeClientRepo> makeSeededClients()
{
    auto repo = std::make_shared<FakeClientRepo>();
    OAuth2Client c;
    c.clientId = "test-client";
    c.clientType = ClientType::CONFIDENTIAL;
    c.clientSecretHash = "secret";
    c.redirectUris = {"https://example.test/cb"};
    c.allowedScopes = {"openid", "profile", "email"};
    repo->clients["test-client"] = c;
    return repo;
}

std::shared_ptr<TokenService> makeService(
  std::shared_ptr<FakeClientRepo> clients,
  std::shared_ptr<FakeGrantRepo> grants,
  std::shared_ptr<FakeTokenRepo> tokens
)
{
    auto crypto = std::make_shared<authforge::common::testing::FakeCryptoProvider>();
    return std::make_shared<TokenService>(clients, grants, tokens, crypto);
}

std::string issueAuthCode(
  TokenService &svc,
  const std::string &clientId,
  const std::string &subject,
  const std::string &scope,
  const std::string &redirectUri
)
{
    std::string rawCode;
    svc.generateAuthorizationCode(
      clientId,
      subject,
      scope,
      redirectUri,
      "",
      "",
      "",
      [&](bool, std::string code, std::string) { rawCode = std::move(code); }
    );
    return rawCode;
}

}  // namespace

TEST(TokenServiceTest, ExchangeCode_HappyPath_ReturnsFrozenShape)
{
    auto clients = makeSeededClients();
    auto grants = std::make_shared<FakeGrantRepo>();
    auto tokens = std::make_shared<FakeTokenRepo>();
    auto svc = makeService(clients, grants, tokens);

    const std::string redirectUri = "https://example.test/cb";
    std::string rawCode = issueAuthCode(*svc, "test-client", "alice", "openid profile", redirectUri);
    ASSERT_FALSE(rawCode.empty());

    Json::Value result;
    bool called = false;
    svc->exchangeCodeForToken(
      rawCode, "test-client", "secret", redirectUri, "", [&](const Json::Value &json) {
          result = json;
          called = true;
      }
    );

    ASSERT_TRUE(called);
    ASSERT_TRUE(result.isMember("access_token"));
    EXPECT_FALSE(result["access_token"].asString().empty());
    EXPECT_EQ(result["token_type"].asString(), "Bearer");
    EXPECT_EQ(result["expires_in"].asInt64(), 3600);
    ASSERT_TRUE(result.isMember("refresh_token"));
    EXPECT_FALSE(result["refresh_token"].asString().empty());
    EXPECT_FALSE(result.isMember("id_token"));
    EXPECT_FALSE(result.isMember("error"));

    std::shared_ptr<OAuth2AccessToken> validated;
    svc->validateAccessToken(result["access_token"].asString(), [&](auto at) { validated = at; });
    ASSERT_NE(validated, nullptr);
    EXPECT_EQ(validated->clientId, "test-client");
    EXPECT_EQ(validated->userId, "alice");
    EXPECT_EQ(validated->scope, "openid profile");
}

TEST(TokenServiceTest, ExchangeCode_WrongSecret_ReturnsInvalidClient)
{
    auto clients = makeSeededClients();
    auto grants = std::make_shared<FakeGrantRepo>();
    auto tokens = std::make_shared<FakeTokenRepo>();
    auto svc = makeService(clients, grants, tokens);
    const std::string redirectUri = "https://example.test/cb";

    std::string rawCode = issueAuthCode(*svc, "test-client", "alice", "openid", redirectUri);
    Json::Value r;
    svc->exchangeCodeForToken(
      rawCode, "test-client", "WRONG", redirectUri, "", [&](const Json::Value &j) { r = j; }
    );
    EXPECT_EQ(r["error"].asString(), "invalid_client");
    EXPECT_EQ(r["error_description"].asString(), "Client authentication failed");
}

TEST(TokenServiceTest, ExchangeCode_UnknownCode_ReturnsInvalidGrant)
{
    auto clients = makeSeededClients();
    auto grants = std::make_shared<FakeGrantRepo>();
    auto tokens = std::make_shared<FakeTokenRepo>();
    auto svc = makeService(clients, grants, tokens);

    Json::Value r;
    svc->exchangeCodeForToken(
      "does-not-exist",
      "test-client",
      "secret",
      "https://example.test/cb",
      "",
      [&](const Json::Value &j) { r = j; }
    );
    EXPECT_EQ(r["error"].asString(), "invalid_grant");
    EXPECT_EQ(r["error_description"].asString(), "Invalid authorization code");
}

TEST(TokenServiceTest, RefreshToken_HappyPathThenReuse_YieldsInvalidGrant)
{
    auto clients = makeSeededClients();
    auto grants = std::make_shared<FakeGrantRepo>();
    auto tokens = std::make_shared<FakeTokenRepo>();
    auto svc = makeService(clients, grants, tokens);
    const std::string redirectUri = "https://example.test/cb";

    std::string rawCode = issueAuthCode(*svc, "test-client", "alice", "openid", redirectUri);
    Json::Value exchanged;
    svc->exchangeCodeForToken(
      rawCode, "test-client", "secret", redirectUri, "", [&](const Json::Value &j) { exchanged = j; }
    );
    const std::string rt = exchanged["refresh_token"].asString();

    Json::Value refreshed;
    bool called = false;
    svc->refreshAccessToken(rt, "test-client", [&](const Json::Value &j) {
        refreshed = j;
        called = true;
    });
    ASSERT_TRUE(called);
    ASSERT_TRUE(refreshed.isMember("access_token"));
    EXPECT_FALSE(refreshed.isMember("error"));

    Json::Value reuse;
    svc->refreshAccessToken(rt, "test-client", [&](const Json::Value &j) { reuse = j; });
    EXPECT_EQ(reuse["error"].asString(), "invalid_grant");
}

TEST(TokenServiceTest, RevokeAccessToken_ThenValidateFails)
{
    auto clients = makeSeededClients();
    auto grants = std::make_shared<FakeGrantRepo>();
    auto tokens = std::make_shared<FakeTokenRepo>();
    auto svc = makeService(clients, grants, tokens);
    const std::string redirectUri = "https://example.test/cb";

    std::string rawCode = issueAuthCode(*svc, "test-client", "alice", "openid", redirectUri);
    Json::Value exchanged;
    svc->exchangeCodeForToken(
      rawCode, "test-client", "secret", redirectUri, "", [&](const Json::Value &j) { exchanged = j; }
    );
    const std::string accessToken = exchanged["access_token"].asString();

    bool revoked = false;
    svc->revokeAccessToken(accessToken, "test-client", [&]() { revoked = true; });
    EXPECT_TRUE(revoked);

    std::shared_ptr<OAuth2AccessToken> after;
    svc->validateAccessToken(accessToken, [&](auto at) { after = at; });
    EXPECT_EQ(after, nullptr);
}

TEST(TokenServiceTest, IntrospectToken_ActiveAndInactive)
{
    auto clients = makeSeededClients();
    auto grants = std::make_shared<FakeGrantRepo>();
    auto tokens = std::make_shared<FakeTokenRepo>();
    auto svc = makeService(clients, grants, tokens);
    const std::string redirectUri = "https://example.test/cb";

    std::string rawCode = issueAuthCode(*svc, "test-client", "alice", "openid", redirectUri);
    Json::Value exchanged;
    svc->exchangeCodeForToken(
      rawCode, "test-client", "secret", redirectUri, "", [&](const Json::Value &j) { exchanged = j; }
    );
    const std::string accessToken = exchanged["access_token"].asString();

    std::optional<TokenIntrospection> active;
    svc->introspectToken(accessToken, [&](auto v) { active = v; });
    ASSERT_TRUE(active.has_value());
    EXPECT_TRUE(active->active);
    EXPECT_EQ(active->clientId, "test-client");

    std::optional<TokenIntrospection> inactive;
    svc->introspectToken("totally-unknown", [&](auto v) { inactive = v; });
    ASSERT_TRUE(inactive.has_value());
    EXPECT_FALSE(inactive->active);
}
