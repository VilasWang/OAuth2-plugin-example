// B2 social account link/unlink: unit tests for fulla::identity::
// SocialLinkService, exercised against the shared FakeOAuthHttpClient +
// FakeSocialAccountRepository doubles (no DB/no Drogon/no real network).
// The flow contract under test is docs/productization-evolution/in-progress/
// social-link-unlink-design.md §4.2.

#ifdef WITH_SOCIAL

#include <fulla/identity/SocialLinkService.h>
#include <fulla/identity/testing/FakeOAuthHttpClient.h>
#include <fulla/identity/testing/FakeSocialAccountRepository.h>
#include <fulla/identity/testing/FakeWebAuthnRepository.h>
#include <fulla/identity/testing/MemorySocialLinkStateStore.h>

#include <gtest/gtest.h>

#include <memory>

using namespace fulla::identity;
using namespace fulla::identity::testing;

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
    // #71: link state store; declared BEFORE svc (members initialize in
    // declaration order and svc's constructor uses it).
    std::shared_ptr<MemorySocialLinkStateStore> stateStore =
      std::make_shared<MemorySocialLinkStateStore>();
    std::shared_ptr<SocialLinkService> svc;

    LinkServiceFixture()
        : svc(std::make_shared<SocialLinkService>(
            github, google, wechat, repo, nullptr, nullptr, stateStore
          ))
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

// #71: the legitimate two-step flow -- mint a state for (user, provider),
// then link carrying it. Most tests only care about the exchange outcome,
// so they go through this helper.
SocialLinkOpResult runLink(
  SocialLinkService &svc,
  const std::string &provider,
  const std::string &code,
  int32_t userId)
{
    SocialLinkOpResult begin;
    svc.beginLink(provider, userId, [&](SocialLinkOpResult r) { begin = std::move(r); });
    if (begin.status != SocialLinkOpStatus::Ok)
        return begin;
    SocialLinkOpResult result;
    svc.linkAccount(
      provider, code, begin.state, userId, [&](SocialLinkOpResult r) { result = std::move(r); });
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

// #74: a missing account repository is a wiring defect, not a client error --
// link answers NotConfigured (500-class), unlink answers RepositoryError
// (matching listAccounts' existing null-repo answer).
TEST(SocialLinkServiceTest, Link_NullAccountRepo_ReturnsNotConfigured)
{
    LinkServiceFixture f;
    SocialLinkService unwired(f.github, f.google, f.wechat, nullptr);

    auto result = runLink(unwired, "github", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::NotConfigured);
}

TEST(SocialLinkServiceTest, Unlink_NullAccountRepo_ReturnsRepositoryError)
{
    LinkServiceFixture f;
    SocialLinkService unwired(f.github, f.google, f.wechat, nullptr);

    auto result = runUnlink(unwired, "github", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::RepositoryError);
}

TEST(SocialLinkServiceTest, Link_ExchangeFailed_TransportError)
{
    LinkServiceFixture f;
    f.http->postFormResponses.push_back(transportFailure());

    auto result = runLink(*f.svc, "github", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::ExchangeFailed);
    EXPECT_EQ(result.errorCode, "NET_CONNECTION_FAILED");
}

// W2 (PR review): Google's login() reads .get("sub", "") -- a 200 userinfo
// response missing the identifier must not reach insertLink (an empty
// subject would permanently claim the UNIQUE(provider, '') slot).
TEST(SocialLinkServiceTest, Link_GoogleMissingSub_ReturnsExchangeFailed)
{
    LinkServiceFixture f;
    Json::Value tokenBody;
    tokenBody["access_token"] = "gtok";
    f.http->postFormResponses.push_back(okJson(tokenBody));
    Json::Value userBody;  // no "sub" field
    userBody["email"] = "x@example.com";
    f.http->getResponses.push_back(okJson(userBody));

    auto result = runLink(*f.svc, "google", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::ExchangeFailed);
    EXPECT_EQ(result.errorCode, "VALIDATION_INVALID_INPUT");
}

// W2: same guard for WeChat's .get("openid", "").
TEST(SocialLinkServiceTest, Link_WeChatMissingOpenid_ReturnsExchangeFailed)
{
    LinkServiceFixture f;
    Json::Value tokenBody;
    tokenBody["access_token"] = "wtok";
    tokenBody["openid"] = "openid-1";  // token step ok...
    f.http->getResponses.push_back(okJson(tokenBody));
    Json::Value userBody;  // ...but userinfo lacks "openid"
    userBody["nickname"] = "nick";
    f.http->getResponses.push_back(okJson(userBody));

    auto result = runLink(*f.svc, "wechat", "code-1", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::ExchangeFailed);
    EXPECT_EQ(result.errorCode, "VALIDATION_INVALID_INPUT");
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


// --------------------- #73: last-credential guard refinements -------------

namespace
{
// Fixture variant wiring a FakeWebAuthnRepository into the service (the
// default fixture passes nullptr, preserving the password-only guard).
struct LinkServiceWithWebAuthnFixture : LinkServiceFixture
{
    std::shared_ptr<FakeWebAuthnRepository> webauthn = std::make_shared<FakeWebAuthnRepository>();

    LinkServiceWithWebAuthnFixture()
        : LinkServiceFixture()
    {
        svc = std::make_shared<SocialLinkService>(github, google, wechat, repo, webauthn);
    }

    void addPasskey(int32_t userId)
    {
        webauthn->credentials["cred-" + std::to_string(userId)] =
          StoredCredential{userId, "pk", "key", 0, 0, std::nullopt};
    }
};
}  // namespace

// #73b: a passkey IS a usable credential -- the last social link of a
// passwordless, passkey-holding user may be unlinked (was a false 409).
TEST(SocialLinkServiceTest, Unlink_LastCredential_WithPasskey_Allowed)
{
    LinkServiceWithWebAuthnFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);
    f.addPasskey(7);

    auto result = runUnlink(*f.svc, "github", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::Ok);
    EXPECT_FALSE(result.lockoutRiskObserved);
    EXPECT_EQ(f.repo->linked.count(FakeSocialAccountRepository::key("github", "4242")), 0u);
}

// #73b control: no password AND no passkey (empty registry) still refuses.
TEST(SocialLinkServiceTest, Unlink_LastCredential_NoPasswordNoPasskey_Refused)
{
    LinkServiceWithWebAuthnFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);

    auto result = runUnlink(*f.svc, "github", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::LastCredentialGuard);
}

// #73b: webAuthnRepo == nullptr (dep not wired) keeps the old password-only
// refusal for a passkey-only user -- unverifiable must not widen to "allow".
TEST(SocialLinkServiceTest, Unlink_LastCredential_NullWebAuthnRepo_OldBehavior)
{
    LinkServiceFixture f;  // no webAuthnRepo wired
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);

    auto result = runUnlink(*f.svc, "github", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::LastCredentialGuard);
}

// #73a: concurrent-unlink race detection. Two links observed up front (guard
// skipped); by the time the post-delete re-read runs, the OTHER link is also
// gone and the user has no usable credential -> Ok + lockoutRiskObserved.
TEST(SocialLinkServiceTest, Unlink_MultiLink_RaceToZeroCredentials_FlagsLockoutRisk)
{
    LinkServiceWithWebAuthnFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);
    f.repo->linked[FakeSocialAccountRepository::key("google", "g1")] = lookupFor(7);

    // Simulate the racing unlink: remove the google link the moment the
    // github delete starts (deleteLink on the fake mutates synchronously, so
    // the re-read inside the Deleted callback sees the empty set).
    f.repo->onDeleteStart = [fakeRepo = f.repo.get()]() {
        fakeRepo->linked.erase(FakeSocialAccountRepository::key("google", "g1"));
    };

    auto result = runUnlink(*f.svc, "github", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::Ok);
    EXPECT_TRUE(result.lockoutRiskObserved);
}

// #73a control: same race shape but the user still has a password -> no flag.
TEST(SocialLinkServiceTest, Unlink_MultiLink_RaceWithPasswordRemaining_NoFlag)
{
    LinkServiceWithWebAuthnFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);
    f.repo->linked[FakeSocialAccountRepository::key("google", "g1")] = lookupFor(7);
    f.repo->usersWithUsablePassword.insert(7);
    f.repo->onDeleteStart = [fakeRepo = f.repo.get()]() {
        fakeRepo->linked.erase(FakeSocialAccountRepository::key("google", "g1"));
    };

    auto result = runUnlink(*f.svc, "github", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::Ok);
    EXPECT_FALSE(result.lockoutRiskObserved);
}

// #73a control: no race (other link still present after delete) -> no flag.
TEST(SocialLinkServiceTest, Unlink_MultiLink_NoRace_NoFlag)
{
    LinkServiceFixture f;
    f.repo->linked[FakeSocialAccountRepository::key("github", "4242")] = lookupFor(7);
    f.repo->linked[FakeSocialAccountRepository::key("google", "g1")] = lookupFor(7);
    f.repo->usersWithUsablePassword.insert(7);  // guard would pass anyway

    auto result = runUnlink(*f.svc, "github", 7);

    EXPECT_EQ(result.status, SocialLinkOpStatus::Ok);
    EXPECT_FALSE(result.lockoutRiskObserved);
    EXPECT_EQ(f.repo->linked.count(FakeSocialAccountRepository::key("google", "g1")), 1u);
}


// --------------------------- #71: link state flow ---------------------------

TEST(SocialLinkServiceTest, BeginLink_MintsUsableState)
{
    LinkServiceFixture f;
    SocialLinkOpResult result;
    f.svc->beginLink("github", 7, [&](SocialLinkOpResult r) { result = std::move(r); });
    EXPECT_EQ(result.status, SocialLinkOpStatus::Ok);
    EXPECT_FALSE(result.state.empty());
}

TEST(SocialLinkServiceTest, BeginLink_InvalidProvider)
{
    LinkServiceFixture f;
    SocialLinkOpResult result;
    f.svc->beginLink("facebook", 7, [&](SocialLinkOpResult r) { result = std::move(r); });
    EXPECT_EQ(result.status, SocialLinkOpStatus::InvalidProvider);
}

TEST(SocialLinkServiceTest, BeginLink_NoStateStore_FailClosed)
{
    // No store wired: linking must not degrade to stateless (#71).
    LinkServiceFixture f;
    f.svc = std::make_shared<SocialLinkService>(f.github, f.google, f.wechat, f.repo);
    SocialLinkOpResult begin;
    f.svc->beginLink("github", 7, [&](SocialLinkOpResult r) { begin = std::move(r); });
    EXPECT_EQ(begin.status, SocialLinkOpStatus::NotConfigured);
    SocialLinkOpResult link;
    f.svc->linkAccount(
      "github", "code", "any-state", 7, [&](SocialLinkOpResult r) { link = std::move(r); });
    EXPECT_EQ(link.status, SocialLinkOpStatus::NotConfigured);
}

TEST(SocialLinkServiceTest, Link_WithoutState_IsRejected)
{
    LinkServiceFixture f;
    SocialLinkOpResult result;
    f.svc->linkAccount(
      "github", "code", "", 7, [&](SocialLinkOpResult r) { result = std::move(r); });
    EXPECT_EQ(result.status, SocialLinkOpStatus::InvalidState);
    // No upstream exchange was attempted (queues untouched).
    EXPECT_TRUE(f.http->postFormResponses.empty());
}

TEST(SocialLinkServiceTest, Link_UnknownState_IsRejected)
{
    LinkServiceFixture f;
    SocialLinkOpResult result;
    f.svc->linkAccount(
      "github", "code", "never-issued", 7, [&](SocialLinkOpResult r) { result = std::move(r); });
    EXPECT_EQ(result.status, SocialLinkOpStatus::InvalidState);
}

TEST(SocialLinkServiceTest, Link_StateBoundToOtherUser_IsRejected)
{
    LinkServiceFixture f;
    SocialLinkOpResult begin;
    f.svc->beginLink("github", 8, [&](SocialLinkOpResult r) { begin = std::move(r); });
    ASSERT_EQ(begin.status, SocialLinkOpStatus::Ok);
    SocialLinkOpResult result;
    f.svc->linkAccount(
      "github", "code", begin.state, 7, [&](SocialLinkOpResult r) { result = std::move(r); });
    EXPECT_EQ(result.status, SocialLinkOpStatus::InvalidState);
}

TEST(SocialLinkServiceTest, Link_StateBoundToOtherProvider_IsRejected)
{
    LinkServiceFixture f;
    SocialLinkOpResult begin;
    f.svc->beginLink("google", 7, [&](SocialLinkOpResult r) { begin = std::move(r); });
    ASSERT_EQ(begin.status, SocialLinkOpStatus::Ok);
    SocialLinkOpResult result;
    f.svc->linkAccount(
      "github", "code", begin.state, 7, [&](SocialLinkOpResult r) { result = std::move(r); });
    EXPECT_EQ(result.status, SocialLinkOpStatus::InvalidState);
}

TEST(SocialLinkServiceTest, Link_StateReplay_IsRejected)
{
    LinkServiceFixture f;
    SocialLinkOpResult begin;
    f.svc->beginLink("github", 7, [&](SocialLinkOpResult r) { begin = std::move(r); });
    ASSERT_EQ(begin.status, SocialLinkOpStatus::Ok);
    f.queueGithub(4242, "octocat");
    SocialLinkOpResult first;
    f.svc->linkAccount(
      "github", "code", begin.state, 7, [&](SocialLinkOpResult r) { first = std::move(r); });
    ASSERT_EQ(first.status, SocialLinkOpStatus::Ok);
    // Replaying the same state (the token was consumed by the first use):
    // rejected before any exchange.
    f.queueGithub(4242, "octocat");
    SocialLinkOpResult replay;
    f.svc->linkAccount(
      "github", "code", begin.state, 7, [&](SocialLinkOpResult r) { replay = std::move(r); });
    EXPECT_EQ(replay.status, SocialLinkOpStatus::InvalidState);
}
