// Task 17 slice 2 (authforge-sdk-refactor): unit tests for the ported
// Domain DTOs (authforge::oauth2::model). Mostly plain-struct field
// round-trips; the one behavior worth asserting is
// TokenIntrospection::toJson()'s RFC 7662 "active: false" short-circuit
// (all other fields omitted when inactive) and ClientType's
// string<->enum round trip / invalid-input rejection.

#include <authforge/oauth2/model/ClientType.h>
#include <authforge/oauth2/model/Dto.h>

#include <gtest/gtest.h>

namespace
{

using namespace authforge::oauth2::model;

TEST(ClientTypeTest, ToString_RoundTrips)
{
    EXPECT_EQ(clientTypeToString(ClientType::PUBLIC), "PUBLIC");
    EXPECT_EQ(clientTypeToString(ClientType::CONFIDENTIAL), "CONFIDENTIAL");
    EXPECT_EQ(stringToClientType("PUBLIC"), ClientType::PUBLIC);
    EXPECT_EQ(stringToClientType("CONFIDENTIAL"), ClientType::CONFIDENTIAL);
}

TEST(ClientTypeTest, StringToClientType_InvalidValue_Throws)
{
    EXPECT_THROW(stringToClientType("bogus"), std::invalid_argument);
}

TEST(OAuth2ClientTest, FieldsRoundTrip)
{
    OAuth2Client client;
    client.clientId = "client-123";
    client.clientType = ClientType::CONFIDENTIAL;
    client.clientSecretHash = "hash";
    client.salt = "salt";
    client.redirectUris = {"https://example.com/callback"};
    client.allowedScopes = {"openid", "profile"};

    EXPECT_EQ(client.clientId, "client-123");
    EXPECT_EQ(client.clientType, ClientType::CONFIDENTIAL);
    ASSERT_EQ(client.redirectUris.size(), 1u);
    EXPECT_EQ(client.redirectUris[0], "https://example.com/callback");
    ASSERT_EQ(client.allowedScopes.size(), 2u);
}

TEST(OAuth2AuthCodeTest, DefaultsToUnused)
{
    OAuth2AuthCode code;
    EXPECT_FALSE(code.used);
}

TEST(OAuth2AccessTokenTest, DefaultsToNotRevoked)
{
    OAuth2AccessToken token;
    EXPECT_FALSE(token.revoked);
    EXPECT_EQ(token.introspectCount, 0);
}

TEST(OAuth2RefreshTokenTest, DefaultsToNotRevoked)
{
    OAuth2RefreshToken token;
    EXPECT_FALSE(token.revoked);
}

TEST(TokenIntrospectionTest, ToJson_Inactive_OnlyEmitsActiveFalse)
{
    TokenIntrospection introspection;
    introspection.active = false;
    introspection.sub = "should-not-appear";

    const Json::Value json = introspection.toJson();

    EXPECT_FALSE(json["active"].asBool());
    EXPECT_FALSE(json.isMember("sub"));
    EXPECT_FALSE(json.isMember("client_id"));
}

TEST(TokenIntrospectionTest, ToJson_Active_EmitsPopulatedFields)
{
    TokenIntrospection introspection;
    introspection.active = true;
    introspection.clientId = "client-123";
    introspection.tokenType = "Bearer";
    introspection.exp = 1000;
    introspection.iat = 500;
    introspection.sub = "alice";
    introspection.aud = "client-123";
    introspection.iss = "https://issuer.example.com";
    introspection.scope = "openid profile";

    const Json::Value json = introspection.toJson();

    EXPECT_TRUE(json["active"].asBool());
    EXPECT_EQ(json["client_id"].asString(), "client-123");
    EXPECT_EQ(json["token_type"].asString(), "Bearer");
    EXPECT_EQ(json["exp"].asInt64(), 1000);
    EXPECT_EQ(json["iat"].asInt64(), 500);
    EXPECT_FALSE(json.isMember("nbf"));  // nbf == 0 -> omitted
    EXPECT_EQ(json["sub"].asString(), "alice");
    EXPECT_EQ(json["aud"].asString(), "client-123");
    EXPECT_EQ(json["iss"].asString(), "https://issuer.example.com");
    EXPECT_EQ(json["scope"].asString(), "openid profile");
}

TEST(AuthorizationTransactionTest, DefaultsToNotConsumed)
{
    AuthorizationTransaction transaction;
    EXPECT_FALSE(transaction.consumed);
}

}  // namespace
