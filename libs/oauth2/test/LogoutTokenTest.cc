// B1 (OIDC Back-Channel Logout 1.0): pure unit tests for the logout_token
// claim builder. Covers AC U1-U3 from the implementation plan (required
// claim set + no nonce, exp=iat+ttl, jti uniqueness). gtest -- no Drogon,
// no DB.

#include <authforge/oauth2/protocol/LogoutToken.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <set>
#include <string>

namespace
{
using authforge::oauth2::protocol::buildLogoutTokenClaims;
using authforge::oauth2::protocol::generateJti;
using authforge::oauth2::protocol::kBackchannelLogoutEventUrn;

// U1: the exact required claim set, the events member keyed by the spec URN,
// and the forbidden/omitted claims (no nonce; sub-only, no sid).
TEST(LogoutTokenTest, ClaimsHaveRequiredSetAndNoNonce)
{
    const auto claims = buildLogoutTokenClaims(
      "https://op.example.com", "user-sub-42", "client-a", 1'000'000, 120, "jti-1");

    EXPECT_EQ(claims["iss"].asString(), "https://op.example.com");
    EXPECT_EQ(claims["sub"].asString(), "user-sub-42");
    EXPECT_EQ(claims["aud"].asString(), "client-a");
    EXPECT_EQ(claims["iat"].asInt64(), 1'000'000);
    EXPECT_EQ(claims["exp"].asInt64(), 1'000'120);
    EXPECT_EQ(claims["jti"].asString(), "jti-1");

    // events member: present, object, keyed by the spec URN -> empty object.
    ASSERT_TRUE(claims.isMember("events"));
    ASSERT_TRUE(claims["events"].isObject());
    ASSERT_TRUE(claims["events"].isMember(kBackchannelLogoutEventUrn));
    EXPECT_TRUE(claims["events"][kBackchannelLogoutEventUrn].isObject());
    EXPECT_EQ(claims["events"][kBackchannelLogoutEventUrn].size(), 0u);

    // Excluded claims: nonce is forbidden by §2.4; we issue sub only (no sid).
    EXPECT_FALSE(claims.isMember("nonce"));
    EXPECT_FALSE(claims.isMember("sid"));
}

// U1 extension: only the seven required members are present (no stray claims).
TEST(LogoutTokenTest, ClaimsContainExactlyTheSevenRequiredMembers)
{
    const auto claims = buildLogoutTokenClaims("iss", "sub", "aud", 10, 120, "j");
    const std::set<std::string> expected{
      "iss", "sub", "aud", "iat", "exp", "jti", "events"};
    std::set<std::string> actual;
    for (auto it = claims.begin(); it != claims.end(); ++it)
        actual.insert(it.name());
    EXPECT_EQ(actual, expected);
}

// U2: exp = iat + ttl (ttl flows through; iss/sub/aud propagate verbatim).
TEST(LogoutTokenTest, ExpRespectsTtlAndClaimsPropagate)
{
    const auto claims = buildLogoutTokenClaims("issuer-x", "subj-y", "aud-z", 500, 77, "jj");
    EXPECT_EQ(claims["exp"].asInt64(), 577);
    EXPECT_EQ(claims["iat"].asInt64(), 500);
    EXPECT_EQ(claims["iss"].asString(), "issuer-x");
    EXPECT_EQ(claims["sub"].asString(), "subj-y");
    EXPECT_EQ(claims["aud"].asString(), "aud-z");
}

// U3: jti is non-empty, 32 hex chars, and unique across many calls.
TEST(LogoutTokenTest, JtiIsUniqueAcrossManyCalls)
{
    std::set<std::string> seen;
    constexpr int N = 1000;
    for (int i = 0; i < N; ++i)
    {
        const auto j = generateJti();
        ASSERT_FALSE(j.empty());
        EXPECT_EQ(j.size(), 32u);
        seen.insert(j);
    }
    EXPECT_EQ(seen.size(), static_cast<std::size_t>(N));
}

}  // namespace
