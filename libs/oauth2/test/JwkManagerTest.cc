// M2b Task 17 slice 10 (authforge-sdk-refactor): basic unit tests for the
// relocated authforge::oauth2::JwkManager. Full concurrency/preservation
// coverage remains in OAuth2Server/test (Property4_JwkBaselineTest.cc/
// CategoryB_JwkManagerRaceTest.cc) -- these are just Domain-layer smoke
// tests confirming the class works standalone (no Drogon, no injected
// logger required).

#include <authforge/oauth2/jwk/JwkManager.h>

#include <gtest/gtest.h>

namespace
{

using authforge::oauth2::JwkManager;

TEST(JwkManagerTest, InitWithoutLogger_GeneratesEphemeralKey)
{
    JwkManager jwk;  // no logger injected -- log() must be a safe no-op
    EXPECT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    EXPECT_TRUE(jwk.isInitialized());
    EXPECT_FALSE(jwk.getKeyId().empty());
}

TEST(JwkManagerTest, SignJwt_BeforeInit_ReturnsEmptyString)
{
    JwkManager jwk;
    EXPECT_EQ(jwk.signJwt(Json::Value(Json::objectValue)), "");
}

TEST(JwkManagerTest, SignJwt_AfterInit_ProducesThreePartToken)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));

    Json::Value claims;
    claims["sub"] = "alice";
    std::string jwt = jwk.signJwt(claims);

    ASSERT_FALSE(jwt.empty());
    EXPECT_EQ(std::count(jwt.begin(), jwt.end(), '.'), 2);
}

TEST(JwkManagerTest, GetJwks_BeforeInit_ReturnsEmptyKeysArray)
{
    JwkManager jwk;
    Json::Value jwks = jwk.getJwks();
    ASSERT_TRUE(jwks.isMember("keys"));
    EXPECT_EQ(jwks["keys"].size(), 0u);
}

TEST(JwkManagerTest, GetJwks_AfterInit_ContainsOneRsaKey)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));

    Json::Value jwks = jwk.getJwks();
    ASSERT_EQ(jwks["keys"].size(), 1u);
    EXPECT_EQ(jwks["keys"][0]["kty"].asString(), "RSA");
    EXPECT_EQ(jwks["keys"][0]["alg"].asString(), "RS256");
    EXPECT_EQ(jwks["keys"][0]["kid"].asString(), jwk.getKeyId());
    EXPECT_FALSE(jwks["keys"][0]["n"].asString().empty());
}

TEST(JwkManagerTest, InitCalledTwice_SecondCallIsNoOpAndReturnsTrue)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    const std::string firstKid = jwk.getKeyId();

    EXPECT_TRUE(jwk.init(Json::Value(Json::objectValue)));  // no-op, not a failure
    EXPECT_EQ(jwk.getKeyId(), firstKid);                    // key unchanged
}

}  // namespace
