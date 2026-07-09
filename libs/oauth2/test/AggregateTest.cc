// Task 17 slice 6 (authforge-sdk-refactor): unit tests for the three
// oauth2::model aggregates (Client/AuthorizationGrant/TokenPair).

#include <authforge/common/testing/FakeCryptoProvider.h>
#include <authforge/oauth2/model/AuthorizationGrant.h>
#include <authforge/oauth2/model/Client.h>
#include <authforge/oauth2/model/TokenPair.h>

#include <gtest/gtest.h>

namespace
{

using namespace authforge::oauth2::model;
using authforge::common::testing::FakeCryptoProvider;

// ---------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------

Client makeClient()
{
    OAuth2Client dto;
    dto.clientId = "client-1";
    dto.clientType = ClientType::PUBLIC;
    dto.redirectUris = {"https://example.com/cb", "https://example.com/cb2"};
    dto.allowedScopes = {"openid", "profile", "email"};
    return Client(dto);
}

TEST(ClientTest, BasicAccessors)
{
    Client client = makeClient();
    EXPECT_EQ(client.clientId(), "client-1");
    EXPECT_TRUE(client.isPublic());
    EXPECT_FALSE(client.isConfidential());
}

TEST(ClientTest, IsRegisteredRedirectUri_ExactMatch_ReturnsTrue)
{
    Client client = makeClient();
    EXPECT_TRUE(client.isRegisteredRedirectUri("https://example.com/cb"));
    EXPECT_FALSE(client.isRegisteredRedirectUri("https://evil.com/cb"));
    EXPECT_FALSE(client.isRegisteredRedirectUri("https://example.com/cb/"));  // no prefix match
}

TEST(ClientTest, AllowsScope_KnownAndUnknownScopes)
{
    Client client = makeClient();
    EXPECT_TRUE(client.allowsScope("openid"));
    EXPECT_FALSE(client.allowsScope("admin"));
}

TEST(ClientTest, AllowsAllScopes_SpaceSeparatedList)
{
    Client client = makeClient();
    EXPECT_TRUE(client.allowsAllScopes("openid profile"));
    EXPECT_FALSE(client.allowsAllScopes("openid admin"));
    EXPECT_TRUE(client.allowsAllScopes(""));  // empty request trivially allowed
}

TEST(ClientTest, Dto_ReturnsUnderlyingStruct)
{
    Client client = makeClient();
    EXPECT_EQ(client.dto().clientId, "client-1");
}

// ---------------------------------------------------------------------
// AuthorizationGrant
// ---------------------------------------------------------------------

AuthorizationTransaction makeTransactionDto()
{
    AuthorizationTransaction dto;
    dto.transactionId = "txn-1";
    dto.clientId = "client-1";
    dto.subject = "local:alice";
    dto.redirectUri = "https://example.com/cb";
    dto.state = "state-abc";
    dto.expiresAt = 9999999999;  // far future
    return dto;
}

TEST(AuthorizationGrantTest, BasicAccessors)
{
    AuthorizationGrant grant(makeTransactionDto());
    EXPECT_EQ(grant.transactionId(), "txn-1");
    EXPECT_EQ(grant.clientId(), "client-1");
    EXPECT_FALSE(grant.consumed());
}

TEST(AuthorizationGrantTest, IsExpired_PastAndFutureTimestamps)
{
    AuthorizationGrant grant(makeTransactionDto());
    EXPECT_FALSE(grant.isExpired(1000));            // far before expiresAt
    EXPECT_TRUE(grant.isExpired(99999999999));      // far after expiresAt
}

TEST(AuthorizationGrantTest, HasPkceChallenge_EmptyByDefault)
{
    AuthorizationGrant grant(makeTransactionDto());
    EXPECT_FALSE(grant.hasPkceChallenge());
}

TEST(AuthorizationGrantTest, VerifyPkceCodeVerifier_S256_CorrectVerifier_Succeeds)
{
    auto dto = makeTransactionDto();
    dto.codeChallenge = "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM";
    dto.codeChallengeMethod = "S256";
    AuthorizationGrant grant(dto);

    ASSERT_TRUE(grant.hasPkceChallenge());

    FakeCryptoProvider crypto;
    EXPECT_TRUE(
      grant.verifyPkceCodeVerifier("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", crypto)
    );
}

TEST(AuthorizationGrantTest, VerifyPkceCodeVerifier_S256_WrongVerifier_Fails)
{
    auto dto = makeTransactionDto();
    dto.codeChallenge = "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM";
    dto.codeChallengeMethod = "S256";
    AuthorizationGrant grant(dto);

    FakeCryptoProvider crypto;
    EXPECT_FALSE(grant.verifyPkceCodeVerifier("wrong-verifier-wrong-verifier-wrong12345", crypto));
}

// ---------------------------------------------------------------------
// TokenPair
// ---------------------------------------------------------------------

TEST(TokenPairTest, ConstructsWithMatchingTokens)
{
    OAuth2AccessToken at;
    at.token = "hashed-access-token";
    at.clientId = "client-1";
    at.userId = "local:alice";
    at.scope = "openid";

    OAuth2RefreshToken rt;
    rt.token = "hashed-refresh-token";
    rt.accessToken = "hashed-access-token";
    rt.clientId = "client-1";
    rt.userId = "local:alice";
    rt.familyId = "family-1";

    TokenPair pair(at, rt);
    EXPECT_EQ(pair.clientId(), "client-1");
    EXPECT_EQ(pair.userId(), "local:alice");
    EXPECT_EQ(pair.familyId(), "family-1");
}

TEST(TokenPairTest, RejectsAccessTokenReferenceMismatch)
{
    OAuth2AccessToken at;
    at.token = "hashed-access-token";
    at.clientId = "client-1";
    at.userId = "local:alice";

    OAuth2RefreshToken rt;
    rt.token = "hashed-refresh-token";
    rt.accessToken = "some-other-token";  // does not reference at.token
    rt.clientId = "client-1";
    rt.userId = "local:alice";

    EXPECT_THROW(TokenPair(at, rt), std::invalid_argument);
}

TEST(TokenPairTest, RejectsClientIdMismatch)
{
    OAuth2AccessToken at;
    at.token = "hashed-access-token";
    at.clientId = "client-1";
    at.userId = "local:alice";

    OAuth2RefreshToken rt;
    rt.token = "hashed-refresh-token";
    rt.accessToken = "hashed-access-token";
    rt.clientId = "client-2";  // mismatch
    rt.userId = "local:alice";

    EXPECT_THROW(TokenPair(at, rt), std::invalid_argument);
}

TEST(TokenPairTest, RejectsUserIdMismatch)
{
    OAuth2AccessToken at;
    at.token = "hashed-access-token";
    at.clientId = "client-1";
    at.userId = "local:alice";

    OAuth2RefreshToken rt;
    rt.token = "hashed-refresh-token";
    rt.accessToken = "hashed-access-token";
    rt.clientId = "client-1";
    rt.userId = "local:bob";  // mismatch

    EXPECT_THROW(TokenPair(at, rt), std::invalid_argument);
}

}  // namespace
