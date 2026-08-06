// M2.5 identity completion, Social auth slice (authforge-sdk-refactor):
// unit tests for authforge::identity::{Google,WeChat,GitHub}AuthService,
// exercised against hand-written fakes for IOAuthHttpClient and
// ISocialAccountRepository (no DB/no Drogon/no real network calls).

#ifdef WITH_SOCIAL

#include <authforge/identity/IOAuthHttpClient.h>
#include <authforge/identity/ISocialAccountRepository.h>
#include <authforge/identity/SocialAuthService.h>
// Shared test doubles (promoted from this file's former anonymous-namespace
// fakes): FakeOAuthHttpClient, FakeSocialAccountRepository, okJson(),
// transportFailure(). See libs/identity/include/authforge/identity/testing/.
#include <authforge/identity/testing/FakeOAuthHttpClient.h>
#include <authforge/identity/testing/FakeSocialAccountRepository.h>

#include <gtest/gtest.h>

#include <deque>
#include <unordered_map>

// Bring the service types (GoogleAuthService, etc.) and the shared test
// doubles (FakeOAuthHttpClient, okJson, ...) into reach at global scope for
// the TEST bodies below. Previously these resolved via an anonymous-namespace
// `using namespace` plus local fake definitions; the fakes now live in
// authforge::identity::testing, so expose both namespaces globally.
using namespace authforge::identity;
using namespace authforge::identity::testing;

// ============================== Google ==============================

TEST(GoogleAuthServiceTest, Login_Success_ReturnsFilteredProfile)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["access_token"] = "gtok-1";
    http->postFormResponses.push_back(okJson(tokenBody));

    Json::Value userBody;
    userBody["sub"] = "1234567890";
    userBody["name"] = "Jane Doe";
    userBody["email"] = "jane@example.com";
    userBody["picture"] = "https://example.test/pic.png";
    userBody["extra_field_should_be_dropped"] = "secret";
    http->getResponses.push_back(okJson(userBody));

    GoogleAuthService svc(http, "client-id", "client-secret", "https://example.test/cb");

    GoogleLoginResult result;
    svc.login("auth-code", [&](GoogleLoginResult r) { result = std::move(r); });

    EXPECT_TRUE(result.errorCode.empty());
    EXPECT_EQ(result.profile.sub, "1234567890");
    EXPECT_EQ(result.profile.name, "Jane Doe");
    EXPECT_EQ(result.profile.email, "jane@example.com");
    EXPECT_EQ(result.profile.picture, "https://example.test/pic.png");

    ASSERT_EQ(http->postFormCalls.size(), 1u);
    EXPECT_EQ(http->postFormCalls[0].url, "https://oauth2.googleapis.com/token");
    ASSERT_EQ(http->getCalls.size(), 1u);
    EXPECT_EQ(http->getCalls[0].bearerToken, "gtok-1");
}

TEST(GoogleAuthServiceTest, Login_TokenExchangeTransportFailure_ReturnsNetError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    http->postFormResponses.push_back(transportFailure());

    GoogleAuthService svc(http, "id", "secret", "redirect");

    GoogleLoginResult result;
    svc.login("code", [&](GoogleLoginResult r) { result = std::move(r); });

    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

TEST(GoogleAuthServiceTest, Login_TokenExchangeNon200_ReturnsNetError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value body;
    http->postFormResponses.push_back(okJson(body, 400));

    GoogleAuthService svc(http, "id", "secret", "redirect");

    GoogleLoginResult result;
    svc.login("code", [&](GoogleLoginResult r) { result = std::move(r); });

    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

TEST(GoogleAuthServiceTest, Login_MissingAccessToken_ReturnsValidationError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value body;
    body["error"] = "invalid_grant";
    http->postFormResponses.push_back(okJson(body));

    GoogleAuthService svc(http, "id", "secret", "redirect");

    GoogleLoginResult result;
    svc.login("code", [&](GoogleLoginResult r) { result = std::move(r); });

    EXPECT_EQ(result.errorCode, "VALIDATION_INVALID_INPUT");
}

TEST(GoogleAuthServiceTest, Login_UserInfoFetchTransportFailure_ReturnsNetError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["access_token"] = "tok";
    http->postFormResponses.push_back(okJson(tokenBody));
    http->getResponses.push_back(transportFailure());

    GoogleAuthService svc(http, "id", "secret", "redirect");

    GoogleLoginResult result;
    svc.login("code", [&](GoogleLoginResult r) { result = std::move(r); });

    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

// ============================== WeChat ==============================

TEST(WeChatAuthServiceTest, Login_Success_ReturnsFilteredProfile)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["access_token"] = "wtok-1";
    tokenBody["openid"] = "open-1";
    http->getResponses.push_back(okJson(tokenBody));

    Json::Value userBody;
    userBody["openid"] = "open-1";
    userBody["nickname"] = "WeChat User";
    userBody["headimgurl"] = "https://example.test/head.png";
    userBody["sex"] = 1;
    userBody["city"] = "Shenzhen";
    userBody["province"] = "Guangdong";
    userBody["country"] = "CN";
    http->getResponses.push_back(okJson(userBody));

    WeChatAuthService svc(http, "appid", "secret");

    WeChatLoginResult result;
    svc.login("auth-code", [&](WeChatLoginResult r) { result = std::move(r); });

    EXPECT_TRUE(result.errorCode.empty());
    EXPECT_EQ(result.profile.openid, "open-1");
    EXPECT_EQ(result.profile.nickname, "WeChat User");
    EXPECT_EQ(result.profile.headimgurl, "https://example.test/head.png");
    EXPECT_EQ(result.profile.sex, 1);
    EXPECT_EQ(result.profile.city, "Shenzhen");
    EXPECT_EQ(result.profile.province, "Guangdong");
    EXPECT_EQ(result.profile.country, "CN");

    ASSERT_EQ(http->getCalls.size(), 2u);
    // WeChat carries credentials in the query string, not a bearer header.
    EXPECT_EQ(http->getCalls[0].bearerToken, "");
    EXPECT_EQ(http->getCalls[1].bearerToken, "");
    EXPECT_NE(http->getCalls[0].url.find("appid=appid"), std::string::npos);
}

TEST(WeChatAuthServiceTest, Login_TokenExchangeTransportFailure_ReturnsNetError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    http->getResponses.push_back(transportFailure());

    WeChatAuthService svc(http, "appid", "secret");

    WeChatLoginResult result;
    svc.login("code", [&](WeChatLoginResult r) { result = std::move(r); });

    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

TEST(WeChatAuthServiceTest, Login_NonZeroErrCode_ReturnsValidationError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["errcode"] = 40029;
    tokenBody["errmsg"] = "invalid code";
    http->getResponses.push_back(okJson(tokenBody));

    WeChatAuthService svc(http, "appid", "secret");

    WeChatLoginResult result;
    svc.login("code", [&](WeChatLoginResult r) { result = std::move(r); });

    EXPECT_EQ(result.errorCode, "VALIDATION_INVALID_INPUT");
}

TEST(WeChatAuthServiceTest, Login_UserInfoFetchTransportFailure_ReturnsNetError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["access_token"] = "tok";
    tokenBody["openid"] = "open-1";
    http->getResponses.push_back(okJson(tokenBody));
    http->getResponses.push_back(transportFailure());

    WeChatAuthService svc(http, "appid", "secret");

    WeChatLoginResult result;
    svc.login("code", [&](WeChatLoginResult r) { result = std::move(r); });

    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

// ============================== GitHub ==============================

TEST(GitHubAuthServiceTest, Login_ExistingLinkedUser_ReturnsExistingUserNoCreate)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["access_token"] = "ghtok-1";
    http->postFormResponses.push_back(okJson(tokenBody));

    Json::Value userBody;
    userBody["login"] = "octocat";
    userBody["email"] = "octocat@example.com";
    userBody["id"] = 42;
    http->getResponses.push_back(okJson(userBody));

    auto repo = std::make_shared<FakeSocialAccountRepository>();
    SocialAccountLookup existing;
    existing.userId = 7;
    existing.username = "gh_octocat";
    repo->linked[FakeSocialAccountRepository::key("github", "42")] = existing;

    GitHubAuthService svc(http, repo, "client-id", "client-secret");

    GitHubLoginResult result;
    svc.login("code", [&](GitHubLoginResult r) { result = std::move(r); });

    EXPECT_TRUE(result.errorCode.empty());
    EXPECT_EQ(result.userId, 7);
    EXPECT_EQ(result.username, "gh_octocat");
    EXPECT_FALSE(result.isNewUser);
}

TEST(GitHubAuthServiceTest, Login_NewUser_CreatesAndLinksAccount)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["access_token"] = "ghtok-1";
    http->postFormResponses.push_back(okJson(tokenBody));

    Json::Value userBody;
    userBody["login"] = "newuser";
    userBody["email"] = "newuser@example.com";
    userBody["id"] = 99;
    http->getResponses.push_back(okJson(userBody));

    auto repo = std::make_shared<FakeSocialAccountRepository>();

    GitHubAuthService svc(http, repo, "client-id", "client-secret");

    GitHubLoginResult result;
    svc.login("code", [&](GitHubLoginResult r) { result = std::move(r); });

    EXPECT_TRUE(result.errorCode.empty());
    EXPECT_TRUE(result.isNewUser);
    EXPECT_EQ(result.username, "gh_newuser");
    EXPECT_GT(result.userId, 0);

    // Second login with the same GitHub id now finds the linked account.
    http->postFormResponses.push_back(okJson(tokenBody));
    http->getResponses.push_back(okJson(userBody));

    GitHubLoginResult secondResult;
    svc.login("code2", [&](GitHubLoginResult r) { secondResult = std::move(r); });

    EXPECT_TRUE(secondResult.errorCode.empty());
    EXPECT_FALSE(secondResult.isNewUser);
    EXPECT_EQ(secondResult.userId, result.userId);
}

TEST(GitHubAuthServiceTest, Login_TokenExchangeTransportFailure_ReturnsNetError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    http->postFormResponses.push_back(transportFailure());
    auto repo = std::make_shared<FakeSocialAccountRepository>();

    GitHubAuthService svc(http, repo, "client-id", "client-secret");

    GitHubLoginResult result;
    svc.login("code", [&](GitHubLoginResult r) { result = std::move(r); });

    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

TEST(GitHubAuthServiceTest, Login_MissingAccessToken_ReturnsValidationError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value body;
    body["error"] = "bad_verification_code";
    http->postFormResponses.push_back(okJson(body));
    auto repo = std::make_shared<FakeSocialAccountRepository>();

    GitHubAuthService svc(http, repo, "client-id", "client-secret");

    GitHubLoginResult result;
    svc.login("code", [&](GitHubLoginResult r) { result = std::move(r); });

    EXPECT_EQ(result.errorCode, "VALIDATION_INVALID_INPUT");
}

TEST(GitHubAuthServiceTest, Login_MissingGitHubLogin_ReturnsValidationError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["access_token"] = "ghtok-1";
    http->postFormResponses.push_back(okJson(tokenBody));

    Json::Value userBody;
    userBody["id"] = 42;  // no "login" field
    http->getResponses.push_back(okJson(userBody));

    auto repo = std::make_shared<FakeSocialAccountRepository>();
    GitHubAuthService svc(http, repo, "client-id", "client-secret");

    GitHubLoginResult result;
    svc.login("code", [&](GitHubLoginResult r) { result = std::move(r); });

    EXPECT_EQ(result.errorCode, "VALIDATION_INVALID_INPUT");
}

TEST(GitHubAuthServiceTest, Login_UserInfoFetchFailure_ReturnsNetError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["access_token"] = "ghtok-1";
    http->postFormResponses.push_back(okJson(tokenBody));
    http->getResponses.push_back(transportFailure());

    auto repo = std::make_shared<FakeSocialAccountRepository>();
    GitHubAuthService svc(http, repo, "client-id", "client-secret");

    GitHubLoginResult result;
    svc.login("code", [&](GitHubLoginResult r) { result = std::move(r); });

    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

TEST(GitHubAuthServiceTest, Login_RepositoryCreateFailure_ReturnsDbError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["access_token"] = "ghtok-1";
    http->postFormResponses.push_back(okJson(tokenBody));

    Json::Value userBody;
    userBody["login"] = "newuser";
    userBody["id"] = 99;
    http->getResponses.push_back(okJson(userBody));

    auto repo = std::make_shared<FakeSocialAccountRepository>();
    repo->failCreate = true;

    GitHubAuthService svc(http, repo, "client-id", "client-secret");

    GitHubLoginResult result;
    svc.login("code", [&](GitHubLoginResult r) { result = std::move(r); });

    EXPECT_EQ(result.errorCode, "DB_QUERY_ERROR");
}

// ---------------------------------------------------------------------------
// Coverage additions (P1): null-httpClient guards for each provider,
// token-exchange non-200 (GitHub/WeChat -- Google already had it), the
// userinfo non-200 branch (GitHub), and WeChat errcode==0 treated as
// success. These pin the documented branching behavior.
// ---------------------------------------------------------------------------

// Google: null httpClient guard -> NET_CONNECTION_FAILED.
TEST(GoogleAuthServiceTest, Login_NullHttpClient_ReturnsNetError)
{
    GoogleAuthService svc(nullptr, "client-id", "client-secret", "https://example.test/cb");
    GoogleLoginResult result;
    svc.login("code", [&](GoogleLoginResult r) { result = std::move(r); });
    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

// Google: userinfo fetch returns non-200 with transportOk=true. The
// service does NOT check statusCode on the userinfo step (only on token
// exchange), so the profile is still returned -- pin this deliberate
// behavior so a future refactor does not silently start rejecting.
TEST(GoogleAuthServiceTest, Login_UserInfoNon200_StillReturnsProfile)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["access_token"] = "gtok-1";
    http->postFormResponses.push_back(okJson(tokenBody));
    Json::Value userBody;
    userBody["sub"] = "s1";
    // userinfo returns 500 but transportOk=true -> profile returned anyway.
    http->getResponses.push_back(okJson(userBody, 500));

    GoogleAuthService svc(http, "client-id", "client-secret", "https://example.test/cb");
    GoogleLoginResult result;
    svc.login("code", [&](GoogleLoginResult r) { result = std::move(r); });
    EXPECT_TRUE(result.errorCode.empty());
    EXPECT_EQ(result.profile.sub, "s1");
}

// GitHub: null httpClient guard -> INTERNAL_ERROR.
TEST(GitHubAuthServiceTest, Login_NullHttpClient_ReturnsInternalError)
{
    auto repo = std::make_shared<FakeSocialAccountRepository>();
    GitHubAuthService svc(nullptr, repo, "client-id", "client-secret");
    GitHubLoginResult result;
    svc.login("code", [&](GitHubLoginResult r) { result = std::move(r); });
    EXPECT_EQ(result.errorCode, "INTERNAL_ERROR");
}

// GitHub: null accountRepo guard -> INTERNAL_ERROR.
TEST(GitHubAuthServiceTest, Login_NullAccountRepo_ReturnsInternalError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    GitHubAuthService svc(http, nullptr, "client-id", "client-secret");
    GitHubLoginResult result;
    svc.login("code", [&](GitHubLoginResult r) { result = std::move(r); });
    EXPECT_EQ(result.errorCode, "INTERNAL_ERROR");
}

// GitHub: token exchange returns non-200 -> NET_CONNECTION_FAILED.
TEST(GitHubAuthServiceTest, Login_TokenExchangeNon200_ReturnsNetError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    http->postFormResponses.push_back(okJson(Json::Value(Json::objectValue), 400));
    auto repo = std::make_shared<FakeSocialAccountRepository>();
    GitHubAuthService svc(http, repo, "client-id", "client-secret");
    GitHubLoginResult result;
    svc.login("code", [&](GitHubLoginResult r) { result = std::move(r); });
    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

// GitHub: userinfo returns non-200 -> NET_CONNECTION_FAILED (GitHub DOES
// check statusCode on the userinfo step, unlike Google).
TEST(GitHubAuthServiceTest, Login_UserInfoNon200_ReturnsNetError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["access_token"] = "ghtok-1";
    http->postFormResponses.push_back(okJson(tokenBody));
    Json::Value userBody;
    userBody["login"] = "user1";
    userBody["id"] = 7;
    http->getResponses.push_back(okJson(userBody, 500));

    auto repo = std::make_shared<FakeSocialAccountRepository>();
    GitHubAuthService svc(http, repo, "client-id", "client-secret");
    GitHubLoginResult result;
    svc.login("code", [&](GitHubLoginResult r) { result = std::move(r); });
    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

// WeChat: null httpClient guard -> NET_CONNECTION_FAILED.
TEST(WeChatAuthServiceTest, Login_NullHttpClient_ReturnsNetError)
{
    WeChatAuthService svc(nullptr, "appid", "secret");
    WeChatLoginResult result;
    svc.login("code", [&](WeChatLoginResult r) { result = std::move(r); });
    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

// WeChat: token body includes errcode=0 (explicitly zero) alongside valid
// access_token/openid -> treated as success. Pins the `errcode != 0`
// condition's complement.
TEST(WeChatAuthServiceTest, Login_ErrCodeZero_TreatedAsSuccess)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    Json::Value tokenBody;
    tokenBody["access_token"] = "wxtok";
    tokenBody["openid"] = "openid-1";
    tokenBody["errcode"] = 0;  // explicitly success
    http->getResponses.push_back(okJson(tokenBody));
    Json::Value userBody;
    userBody["openid"] = "openid-1";
    userBody["nickname"] = "nick";
    http->getResponses.push_back(okJson(userBody));

    WeChatAuthService svc(http, "appid", "secret");
    WeChatLoginResult result;
    svc.login("code", [&](WeChatLoginResult r) { result = std::move(r); });
    EXPECT_TRUE(result.errorCode.empty());
    EXPECT_EQ(result.profile.openid, "openid-1");
}

// WeChat: token exchange returns non-200 -> NET_CONNECTION_FAILED.
TEST(WeChatAuthServiceTest, Login_TokenExchangeNon200_ReturnsNetError)
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    http->getResponses.push_back(okJson(Json::Value(Json::objectValue), 400));
    WeChatAuthService svc(http, "appid", "secret");
    WeChatLoginResult result;
    svc.login("code", [&](WeChatLoginResult r) { result = std::move(r); });
    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

#endif  // WITH_SOCIAL
