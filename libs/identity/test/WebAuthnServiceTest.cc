// M2.5 identity completion (authforge-sdk-refactor): unit tests for
// authforge::identity::WebAuthnService, exercised against a minimal
// in-memory fake IWebAuthnRepository (no DB/no Drogon).

#include <authforge/common/testing/FakeCryptoProvider.h>
#include <authforge/identity/IWebAuthnRepository.h>
#include <authforge/identity/WebAuthnService.h>
// Shared test double (promoted from this file's former anonymous-namespace
// fake): FakeWebAuthnRepository + StoredCredential. See
// libs/identity/include/authforge/identity/testing/.
#include <authforge/identity/testing/FakeWebAuthnRepository.h>

#include <gtest/gtest.h>

#include <memory>

using namespace authforge::identity;
using authforge::common::testing::FakeCryptoProvider;
using authforge::identity::testing::FakeWebAuthnRepository;

namespace
{

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

// ---------------------------------------------------------------------------
// Coverage additions (P1): null-dependency guards for every method
// (WebAuthnService.cc:45,66,109,126,168), the StoreCredentialOutcome::Error
// -> DB_QUERY_ERROR branch (cc:96-99), and custom rpId/rpName propagation.
// Null-guard tests construct WebAuthnService directly with nullptr deps.
// ---------------------------------------------------------------------------

// beginRegistration: null crypto guard -> nullopt (cc:45).
TEST(WebAuthnServiceTest, BeginRegistration_NullCrypto_ReturnsNullopt)
{
    WebAuthnService svc(nullptr, nullptr);
    std::optional<WebAuthnRegistrationChallenge> result = WebAuthnRegistrationChallenge{};
    svc.beginRegistration([&](auto r) { result = std::move(r); });
    EXPECT_FALSE(result.has_value());
}

// beginAuthentication: null crypto guard -> nullopt (cc:109).
TEST(WebAuthnServiceTest, BeginAuthentication_NullCrypto_ReturnsNullopt)
{
    WebAuthnService svc(nullptr, nullptr);
    std::optional<WebAuthnAuthenticationChallenge> result = WebAuthnAuthenticationChallenge{};
    svc.beginAuthentication([&](auto r) { result = std::move(r); });
    EXPECT_FALSE(result.has_value());
}

// finishRegistration: null repo guard -> INTERNAL_ERROR (cc:66).
TEST(WebAuthnServiceTest, FinishRegistration_NullRepo_ReturnsInternalError)
{
    WebAuthnService svc(nullptr, nullptr);
    std::string err = "none";
    svc.finishRegistration(42, "cred-1", "pubkey-1", "Laptop", [&](const std::string &e) { err = e; });
    EXPECT_EQ(err, "INTERNAL_ERROR");
}

// finishRegistration: storeCredential returns Error -> DB_QUERY_ERROR
// (cc:96-99). The default Error arm was unreachable with the original fake.
TEST(WebAuthnServiceTest, FinishRegistration_StoreError_ReturnsDbQueryError)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    repo->forceStoreError = true;
    auto svc = makeService(repo);
    std::string err = "none";
    svc->finishRegistration(42, "cred-1", "pubkey-1", "Laptop", [&](const std::string &e) { err = e; });
    EXPECT_EQ(err, "DB_QUERY_ERROR");
}

// finishAuthentication: null repo guard -> nullopt (cc:126).
TEST(WebAuthnServiceTest, FinishAuthentication_NullRepo_ReturnsNullopt)
{
    WebAuthnService svc(nullptr, nullptr);
    std::optional<WebAuthnAuthResult> result = WebAuthnAuthResult{};
    svc.finishAuthentication("cred-1", [&](auto r) { result = std::move(r); });
    EXPECT_FALSE(result.has_value());
}

// listCredentials: null repo guard -> empty vector (cc:168).
TEST(WebAuthnServiceTest, ListCredentials_NullRepo_ReturnsEmpty)
{
    WebAuthnService svc(nullptr, nullptr);
    std::vector<WebAuthnCredentialSummary> result;
    result.push_back({});  // sentinel so emptiness is observable
    svc.listCredentials(42, [&](auto r) { result = std::move(r); });
    EXPECT_TRUE(result.empty());
}

// Constructor: custom rpId/rpName propagate into begin-registration and
// begin-authentication challenges (cc:28-39,52-54,116-117). The existing
// tests only exercise the default values.
TEST(WebAuthnServiceTest, Constructor_CustomRpIdAndRpName_PropagatedToChallenges)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto crypto = std::make_shared<FakeCryptoProvider>();
    WebAuthnService svc(repo, crypto, "auth.example.com", "Custom Issuer");

    std::optional<WebAuthnRegistrationChallenge> reg;
    svc.beginRegistration([&](auto r) { reg = std::move(r); });
    ASSERT_TRUE(reg.has_value());
    EXPECT_EQ(reg->rpId, "auth.example.com");
    EXPECT_EQ(reg->rpName, "Custom Issuer");

    std::optional<WebAuthnAuthenticationChallenge> auth;
    svc.beginAuthentication([&](auto r) { auth = std::move(r); });
    ASSERT_TRUE(auth.has_value());
    EXPECT_EQ(auth->rpId, "auth.example.com");
}

// ---------------------------------------------------------------------------
// Coverage additions (P3): beginAuthentication produces a different
// challenge on each call (mirror of the existing registration test).
// ---------------------------------------------------------------------------

// beginAuthentication: two consecutive calls yield distinct challenges
// (WebAuthnService.cc:105-119, mirrors BeginRegistration_TwoCallsProduceDifferentChallenges).
TEST(WebAuthnServiceTest, BeginAuthentication_TwoCallsProduceDifferentChallenges)
{
    auto repo = std::make_shared<FakeWebAuthnRepository>();
    auto svc = makeService(repo);

    std::optional<WebAuthnAuthenticationChallenge> first;
    svc->beginAuthentication([&](auto r) { first = std::move(r); });
    std::optional<WebAuthnAuthenticationChallenge> second;
    svc->beginAuthentication([&](auto r) { second = std::move(r); });

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(first->challenge.empty());
    EXPECT_NE(first->challenge, second->challenge);
}
