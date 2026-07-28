// M2.5 identity completion (authforge-sdk-refactor): unit tests for
// authforge::identity::WebAuthnService, exercised against a minimal
// in-memory fake IWebAuthnRepository (no DB/no Drogon).

#include <authforge/common/testing/FakeCryptoProvider.h>
#include <authforge/identity/IWebAuthnRepository.h>
#include <authforge/identity/WebAuthnService.h>

#include <gtest/gtest.h>

#include <unordered_map>

namespace
{

using namespace authforge::identity;
using authforge::common::testing::FakeCryptoProvider;

struct StoredCredential
{
    int32_t userId = 0;
    std::string publicKey;
    std::string name;
    int signCount = 0;
    int64_t createdAt = 0;
    std::optional<int64_t> lastUsedAt;
};

class FakeWebAuthnRepository : public IWebAuthnRepository
{
  public:
    std::unordered_map<std::string, StoredCredential> credentials;  // keyed by credential_id
    int64_t nextCreatedAt = 1000;

    void storeCredential(
      int32_t userId,
      const std::string &credentialId,
      const std::string &publicKey,
      const std::string &name,
      StoreCredentialCallback &&cb
    ) override
    {
        if (credentials.count(credentialId) != 0)
        {
            cb(StoreCredentialOutcome::DuplicateCredentialId);
            return;
        }

        StoredCredential cred;
        cred.userId = userId;
        cred.publicKey = publicKey;
        cred.name = name;
        cred.signCount = 0;
        cred.createdAt = nextCreatedAt++;
        credentials[credentialId] = cred;
        cb(StoreCredentialOutcome::Success);
    }

    void findByCredentialId(const std::string &credentialId, CredentialLookupCallback &&cb) override
    {
        auto it = credentials.find(credentialId);
        if (it == credentials.end())
        {
            cb(std::nullopt);
            return;
        }
        WebAuthnCredentialLookup lookup;
        lookup.userId = it->second.userId;
        lookup.publicSub = "sub-" + std::to_string(it->second.userId);
        lookup.signCount = it->second.signCount;
        cb(lookup);
    }

    void updateSignCount(
      const std::string &credentialId,
      int newSignCount,
      BoolCallback &&cb
    ) override
    {
        auto it = credentials.find(credentialId);
        if (it == credentials.end())
        {
            cb(false);
            return;
        }
        it->second.signCount = newSignCount;
        it->second.lastUsedAt = 12345;
        cb(true);
    }

    void listCredentials(int32_t userId, ListCredentialsCallback &&cb) override
    {
        std::vector<WebAuthnCredentialSummary> result;
        for (const auto &[credentialId, cred] : credentials)
        {
            if (cred.userId != userId)
                continue;
            WebAuthnCredentialSummary summary;
            summary.credentialId = credentialId;
            summary.name = cred.name;
            summary.signCount = cred.signCount;
            summary.createdAt = cred.createdAt;
            summary.lastUsedAt = cred.lastUsedAt;
            result.push_back(summary);
        }
        cb(std::move(result));
    }
};

std::shared_ptr<WebAuthnService> makeService(std::shared_ptr<FakeWebAuthnRepository> repo)
{
    auto crypto = std::make_shared<FakeCryptoProvider>();
    return std::make_shared<WebAuthnService>(repo, crypto);
}

}  // namespace

TEST(WebAuthnServiceTest, BeginRegistration_GeneratesChallengeAndRpInfo)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    std::optional<WebAuthnRegistrationChallenge> result;
    svc->beginRegistration([&](auto r) { result = std::move(r); });

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->challenge.empty());
    EXPECT_EQ(result->rpId, "localhost");
    EXPECT_EQ(result->rpName, "OAuth2 Server");
    EXPECT_EQ(result->timeoutMs, 60000);
}

TEST(WebAuthnServiceTest, BeginRegistration_TwoCallsProduceDifferentChallenges)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    std::optional<WebAuthnRegistrationChallenge> r1;
    std::optional<WebAuthnRegistrationChallenge> r2;
    svc->beginRegistration([&](auto r) { r1 = std::move(r); });
    svc->beginRegistration([&](auto r) { r2 = std::move(r); });

    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_NE(r1->challenge, r2->challenge);
}

TEST(WebAuthnServiceTest, FinishRegistration_StoresCredentialAndReturnsEmptyError)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    std::string errorCode = "unset";
    svc->finishRegistration(42, "cred-1", "pubkey-1", "My Passkey", [&](const std::string &code) {
        errorCode = code;
    });

    EXPECT_EQ(errorCode, "");
    ASSERT_EQ(repo->credentials.count("cred-1"), 1u);
    EXPECT_EQ(repo->credentials["cred-1"].userId, 42);
    EXPECT_EQ(repo->credentials["cred-1"].publicKey, "pubkey-1");
    EXPECT_EQ(repo->credentials["cred-1"].name, "My Passkey");
    EXPECT_EQ(repo->credentials["cred-1"].signCount, 0);
}

TEST(WebAuthnServiceTest, FinishRegistration_EmptyNameDefaultsToPasskey)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    std::string errorCode;
    svc->finishRegistration(42, "cred-1", "pubkey-1", "", [&](const std::string &code) {
        errorCode = code;
    });

    EXPECT_EQ(errorCode, "");
    EXPECT_EQ(repo->credentials["cred-1"].name, "Passkey");
}

TEST(WebAuthnServiceTest, FinishRegistration_MissingCredentialId_ReturnsValidationError)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    std::string errorCode;
    svc->finishRegistration(42, "", "pubkey-1", "Passkey", [&](const std::string &code) {
        errorCode = code;
    });

    EXPECT_EQ(errorCode, "VALIDATION_MISSING_REQUIRED_FIELD");
    EXPECT_TRUE(repo->credentials.empty());
}

TEST(WebAuthnServiceTest, FinishRegistration_MissingPublicKey_ReturnsValidationError)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    std::string errorCode;
    svc->finishRegistration(42, "cred-1", "", "Passkey", [&](const std::string &code) {
        errorCode = code;
    });

    EXPECT_EQ(errorCode, "VALIDATION_MISSING_REQUIRED_FIELD");
    EXPECT_TRUE(repo->credentials.empty());
}

TEST(WebAuthnServiceTest, FinishRegistration_DuplicateCredentialId_ReturnsConflictError)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    svc->finishRegistration(42, "cred-1", "pubkey-1", "Passkey", [](const std::string &) {});

    std::string errorCode;
    svc->finishRegistration(99, "cred-1", "pubkey-2", "Other", [&](const std::string &code) {
        errorCode = code;
    });

    EXPECT_EQ(errorCode, "VALIDATION_CREDENTIAL_ALREADY_REGISTERED");
    // Original credential is untouched by the rejected duplicate attempt.
    EXPECT_EQ(repo->credentials["cred-1"].userId, 42);
    EXPECT_EQ(repo->credentials["cred-1"].publicKey, "pubkey-1");
}

TEST(WebAuthnServiceTest, BeginAuthentication_GeneratesChallengeAndRpId)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    std::optional<WebAuthnAuthenticationChallenge> result;
    svc->beginAuthentication([&](auto r) { result = std::move(r); });

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->challenge.empty());
    EXPECT_EQ(result->rpId, "localhost");
    EXPECT_EQ(result->timeoutMs, 60000);
}

TEST(WebAuthnServiceTest, FinishAuthentication_UnknownCredential_ReturnsNullopt)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    std::optional<WebAuthnAuthResult> result;
    svc->finishAuthentication("no-such-cred", [&](auto r) { result = std::move(r); });

    EXPECT_FALSE(result.has_value());
}

TEST(WebAuthnServiceTest, FinishAuthentication_EmptyCredentialId_ReturnsNullopt)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    std::optional<WebAuthnAuthResult> result;
    svc->finishAuthentication("", [&](auto r) { result = std::move(r); });

    EXPECT_FALSE(result.has_value());
}

TEST(WebAuthnServiceTest, FinishAuthentication_KnownCredential_SucceedsAndIncrementsSignCount)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    svc->finishRegistration(42, "cred-1", "pubkey-1", "Passkey", [](const std::string &) {});
    ASSERT_EQ(repo->credentials["cred-1"].signCount, 0);

    std::optional<WebAuthnAuthResult> result;
    svc->finishAuthentication("cred-1", [&](auto r) { result = std::move(r); });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->userId, 42);
    EXPECT_EQ(result->publicSub, "sub-42");
    EXPECT_EQ(result->signCount, 1);
    EXPECT_EQ(repo->credentials["cred-1"].signCount, 1);
}

TEST(WebAuthnServiceTest, FinishAuthentication_RepeatedCalls_KeepIncrementingSignCount)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    svc->finishRegistration(42, "cred-1", "pubkey-1", "Passkey", [](const std::string &) {});

    std::optional<WebAuthnAuthResult> first;
    svc->finishAuthentication("cred-1", [&](auto r) { first = std::move(r); });
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->signCount, 1);

    std::optional<WebAuthnAuthResult> second;
    svc->finishAuthentication("cred-1", [&](auto r) { second = std::move(r); });
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->signCount, 2);
}

TEST(WebAuthnServiceTest, ListCredentials_ReturnsOnlyMatchingUsersCredentials)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    svc->finishRegistration(42, "cred-1", "pubkey-1", "Laptop", [](const std::string &) {});
    svc->finishRegistration(42, "cred-2", "pubkey-2", "Phone", [](const std::string &) {});
    svc->finishRegistration(99, "cred-3", "pubkey-3", "Other User", [](const std::string &) {});

    std::vector<WebAuthnCredentialSummary> result;
    svc->listCredentials(42, [&](auto r) { result = std::move(r); });

    ASSERT_EQ(result.size(), 2u);
    for (const auto &summary : result)
    {
        EXPECT_TRUE(summary.credentialId == "cred-1" || summary.credentialId == "cred-2");
        EXPECT_EQ(summary.signCount, 0);
    }
}

TEST(WebAuthnServiceTest, ListCredentials_NoCredentials_ReturnsEmpty)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    std::vector<WebAuthnCredentialSummary> result;
    svc->listCredentials(7, [&](auto r) { result = std::move(r); });

    EXPECT_TRUE(result.empty());
}

TEST(WebAuthnServiceTest, ListCredentials_ReflectsSignCountAfterAuthentication)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    svc->finishRegistration(42, "cred-1", "pubkey-1", "Laptop", [](const std::string &) {});
    svc->finishAuthentication("cred-1", [](auto) {});

    std::vector<WebAuthnCredentialSummary> result;
    svc->listCredentials(42, [&](auto r) { result = std::move(r); });

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].signCount, 1);
    EXPECT_TRUE(result[0].lastUsedAt.has_value());
}
