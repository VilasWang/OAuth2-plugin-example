// B2 social account link/unlink: unit tests for authforge::identity::
// SocialLinkService, exercised against the shared FakeOAuthHttpClient +
// FakeSocialAccountRepository doubles (no DB/no Drogon/no real network).
// The flow contract under test is docs/productization-evolution/in-progress/
// social-link-unlink-design.md §4.2.

#ifdef WITH_SOCIAL

#include <authforge/identity/SocialLinkService.h>
#include <authforge/identity/testing/FakeOAuthHttpClient.h>
#include <authforge/identity/testing/FakeSocialAccountRepository.h>

#include <gtest/gtest.h>

#include <memory>

using namespace authforge::identity;
using namespace authforge::identity::testing;

namespace
{

// One shared wiring: the three provider services + link service, all backed
// by the same fakes. Tests prime the http response queues per provider:
//   github: postFormResponses[token] + getResponses[/user]
//   google: postFormResponses[token] + getResponses[userinfo]
//   wechat: getResponses[token] + getResponses[userinfo]
struct LinkServiceFixture
{
    std::shared_ptr<FakeOAuthHttpClient> http = std::make_shared<FakeOAuthHttpClient>();
    std::shared_ptr<FakeSocialAccountRepository> repo = std::make_shared<FakeSocialAccountRepository>();
    std::shared_ptr<GitHubAuthService> github =
      std::make_shared<GitHubAuthService>(http, repo, "gh-id", "gh-secret");
    std::shared_ptr<GoogleAuthService> google =
      std::make_shared<GoogleAuthService>(http, "g-id", "g-secret", "https://example.test/cb");
    std::shared_ptr<WeChatAuthService> wechat =
      std::make_shared<WeChatAuthService>(http, "wx-appid", "wx-secret");
    std::shared_ptr<SocialLinkService> svc;

    LinkServiceFixture()
        : svc(std::make_shared<SocialLinkService>(github, google, wechat, repo))
    {
    }

    void queueGithub(int64_t id, const std::string &login)
    {
        Json::Value tokenBody;
        tokenBody["access_token"] = "ghtok";
        http->postFormResponses.push_back(okJson(tokenBody));
        Json::Value userBody;
        userBody["id"] = id;
        userBody["login"] = login;
        http->getResponses.push_back(okJson(userBody));
    }

    void queueGoogle(const std::string &sub)
    {
        Json::Value tokenBody;
        tokenBody["access_token"] = "gtok";
        http->postFormResponses.push_back(okJson(tokenBody));
        Json::Value userBody;
        userBody["sub"] = sub;
        http->getResponses.push_back(okJson(userBody));
    }

    void queueWeChat(const std::string &openid)
    {
        Json::Value tokenBody;
        tokenBody["access_token"] = "wtok";
        tokenBody["openid"] = openid;
        http->getResponses.push_back(okJson(tokenBody));
        Json::Value userBody;
        userBody["openid"] = openid;
        http->getResponses.push_back(okJson(userBody));
    }
};

SocialLinkOpResult runLink(
  SocialLinkService &svc,
  const std::string &provider,
  const std::string &code,
  int32_t userId)
{
    SocialLinkOpResult result;
    svc.linkAccount(provider, code, userId, [&](SocialLinkOpResult r) { result = std::move(r); });
    return result;
}

SocialLinkOpResult runUnlink(SocialLinkService &svc, const std::string &provider, int32_t userId)
{
    SocialLinkOpResult result;
    svc.unlinkAccount(provider, userId, [&](SocialLinkOpResult r) { result = std::move(r); });
    return result;
}

SocialAccountLookup lookupFor(int32_t userId)
{
    SocialAccountLookup lookup;
    lookup.userId = userId;
    lookup.username = "u" + std::to_string(userId);
    return lookup;
}

}  // namespace

// ------------------------------ link: happy paths -------------------------

TEST(SocialLinkServiceTest, Link_GitHub_Success)
{
    LinkServiceFixture f;
    f.queueGithub(4242, "octocat");

    auto result = runLink(*f.svc, "github", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::Ok);
    EXPECT_EQ(result.entry.provider, "github");
    EXPECT_EQ(result.entry.subject, "4242");
    // The mapping row is visible to findLinkedUser (login flow) afterwards.
    EXPECT_TRUE(f.repo->linked.count(FakeSocialAccountRepository::key("github", "4242")) > 0);
}

TEST(SocialLinkServiceTest, Link_Google_Success)
{
    LinkServiceFixture f;
    f.queueGoogle("google-sub-1");

    auto result = runLink(*f.svc, "google", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::Ok);
    EXPECT_EQ(result.entry.provider, "google");
    EXPECT_EQ(result.entry.subject, "google-sub-1");
}

TEST(SocialLinkServiceTest, Link_WeChat_Success)
{
    LinkServiceFixture f;
    f.queueWeChat("wx-openid-1");

    auto result = runLink(*f.svc, "wechat", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::Ok);
    EXPECT_EQ(result.entry.provider, "wechat");
    EXPECT_EQ(result.entry.subject, "wx-openid-1");
}

// ------------------------------ link: failures ----------------------------

TEST(SocialLinkServiceTest, Link_InvalidProvider)
{
    LinkServiceFixture f;
    auto result = runLink(*f.svc, "facebook", "code", 7);
    EXPECT_EQ(result.status, SocialLinkOpStatus::InvalidProvider);
}

// The provider's service was never injected -> NotConfigured (the controller
// maps this to 500, distinct from an unsupported provider's 400).
TEST(SocialLinkServiceTest, Link_NotConfigured_MissingProviderService)
{
    LinkServiceFixture f;
    SocialLinkService halfWired(f.github, nullptr, f.wechat, f.repo);

    auto result = runLink(halfWired, "google", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::NotConfigured);
}

TEST(SocialLinkServiceTest, Link_ExchangeFailed_TransportError)
{
    LinkServiceFixture f;
    f.http->postFormResponses.push_back(transportFailure());

    auto result = runLink(*f.svc, "github", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::ExchangeFailed);
    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

TEST(SocialLinkServiceTest, Link_AlreadyLinkedToSelf)
{
    LinkServiceFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);
    f.queueGithub(4242, "octocat");

    auto result = runLink(*f.svc, "github", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::AlreadyLinkedToSelf);
}

TEST(SocialLinkServiceTest, Link_AlreadyLinkedToOtherUser)
{
    LinkServiceFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(8);
    f.queueGithub(4242, "octocat");

    auto result = runLink(*f.svc, "github", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::AlreadyLinkedToOtherUser);
}

// A mapping held by a soft-deleted/locked account reports the same
// AlreadyLinkedToOtherUser wording (no account-status enumeration -- design
// §4.2, mirroring findLinkedUser's own #54 rule).
TEST(SocialLinkServiceTest, Link_MappingHeldByUnavailableAccount_SameConflictWording)
{
    LinkServiceFixture f;
    f.repo->unavailableKeys.insert(FakeSocialAccountRepository::key("github", "4242"));
    f.queueGithub(4242, "octocat");

    auto result = runLink(*f.svc, "github", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::AlreadyLinkedToOtherUser);
}

// The user already linked a DIFFERENT GitHub account (subject 111); linking
// subject 4242 must demand an explicit unlink first (design D5).
TEST(SocialLinkServiceTest, Link_ProviderConflictForUser)
{
    LinkServiceFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "111")] = lookupFor(7);
    f.queueGithub(4242, "octocat");

    auto result = runLink(*f.svc, "github", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::ProviderConflictForUser);
}

// The UNIQUE(provider, subject) race backstop: pre-checks passed but the
// insert lost the race to another user.
TEST(SocialLinkServiceTest, Link_InsertRaceConflict)
{
    LinkServiceFixture f;
    f.repo->forceInsertConflict = true;
    f.queueGithub(4242, "octocat");

    auto result = runLink(*f.svc, "github", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::AlreadyLinkedToOtherUser);
}

TEST(SocialLinkServiceTest, Link_RepositoryErrorOnLookup)
{
    LinkServiceFixture f;
    f.repo->failFind = true;
    f.queueGithub(4242, "octocat");

    auto result = runLink(*f.svc, "github", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::RepositoryError);
}

TEST(SocialLinkServiceTest, Link_RepositoryErrorOnInsert)
{
    LinkServiceFixture f;
    f.repo->failInsert = true;
    f.queueGithub(4242, "octocat");

    auto result = runLink(*f.svc, "github", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::RepositoryError);
}

// ------------------------------ unlink -------------------------------------

TEST(SocialLinkServiceTest, Unlink_Success)
{
    LinkServiceFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);
    // Two links -> the last-credential guard does not apply.
    f.repo->linked[FakeSocialAccountRepository::key("google", "g1")] = lookupFor(7);

    auto result = runUnlink(*f.svc, "github", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::Ok);
    EXPECT_EQ(result.entry.provider, "github");
    EXPECT_EQ(result.entry.subject, "4242");
    EXPECT_EQ(f.repo->linked.count(FakeSocialAccountRepository::key("github", "4242")), 0u);
    // The other provider's link is untouched.
    EXPECT_EQ(f.repo->linked.count(FakeSocialAccountRepository::key("google", "g1")), 1u);
}

TEST(SocialLinkServiceTest, Unlink_NoLink)
{
    LinkServiceFixture f;
    auto result = runUnlink(*f.svc, "github", 7);
    EXPECT_EQ(result.status, SocialLinkOpStatus::NoLink);
}

TEST(SocialLinkServiceTest, Unlink_InvalidProvider)
{
    LinkServiceFixture f;
    auto result = runUnlink(*f.svc, "facebook", 7);
    EXPECT_EQ(result.status, SocialLinkOpStatus::InvalidProvider);
}

// Last social link + no usable password -> refused (lockout guard).
TEST(SocialLinkServiceTest, Unlink_LastCredentialGuard_NoPassword)
{
    LinkServiceFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);
    // usersWithUsablePassword does not contain 7 -> no usable password.

    auto result = runUnlink(*f.svc, "github", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::LastCredentialGuard);
    // The mapping row is kept.
    EXPECT_EQ(f.repo->linked.count(FakeSocialAccountRepository::key("github", "4242")), 1u);
}

// Last social link but the user HAS a usable password -> allowed.
TEST(SocialLinkServiceTest, Unlink_LastCredential_WithPassword_Allowed)
{
    LinkServiceFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);
    f.repo->usersWithUsablePassword.insert(7);

    auto result = runUnlink(*f.svc, "github", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::Ok);
}

// Guard's repository failure -> RepositoryError (fail-safe, not "allow").
TEST(SocialLinkServiceTest, Unlink_PasswordCheckFailure_RepositoryError)
{
    LinkServiceFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);
    f.repo->failPasswordCheck = true;

    auto result = runUnlink(*f.svc, "github", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::RepositoryError);
}

TEST(SocialLinkServiceTest, Unlink_RepositoryErrorOnList)
{
    LinkServiceFixture f;
    f.repo->failList = true;
    auto result = runUnlink(*f.svc, "github", 7);
    EXPECT_EQ(result.status, SocialLinkOpStatus::RepositoryError);
}

TEST(SocialLinkServiceTest, Unlink_RepositoryErrorOnDelete)
{
    LinkServiceFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);
    f.repo->linked[FakeSocialAccountRepository::key("google", "g1")] = lookupFor(7);
    f.repo->failDelete = true;

    auto result = runUnlink(*f.svc, "github", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::RepositoryError);
}

// ------------------------------ list ---------------------------------------

TEST(SocialLinkServiceTest, List_ReturnsUserEntriesOnly)
{
    LinkServiceFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);
    f.repo->linked[FakeSocialAccountRepository::key("google", "g1")] = lookupFor(7);
    f.repo->linked[FakeSocialAccountRepository::key("wechat", "wx1")] = lookupFor(8);

    SocialLinkOpStatus status = SocialLinkOpStatus::RepositoryError;
    std::vector<SocialLinkEntry> entries;
    f.svc->listAccounts(
      7, [&](SocialLinkOpStatus s, std::vector<SocialLinkEntry> e) {
          status = s;
          entries = std::move(e);
      }
    );

    EXPECT_EQ(status, SocialLinkOpStatus::Ok);
    EXPECT_EQ(entries.size(), 2u);
    for (const auto &e : entries)
    {
        EXPECT_EQ(e.provider == "github" || e.provider == "google", true);
        EXPECT_FALSE(e.linkedAt.empty());
    }
}

TEST(SocialLinkServiceTest, List_RepositoryError)
{
    LinkServiceFixture f;
    f.repo->failList = true;
    SocialLinkOpStatus status = SocialLinkOpStatus::Ok;
    std::vector<SocialLinkEntry> entries;
    f.svc->listAccounts(
      7, [&](SocialLinkOpStatus s, std::vector<SocialLinkEntry> e) {
          status = s;
          entries = std::move(e);
      }
    );
    EXPECT_EQ(status, SocialLinkOpStatus::RepositoryError);
    EXPECT_TRUE(entries.empty());
}

#endif  // WITH_SOCIAL
