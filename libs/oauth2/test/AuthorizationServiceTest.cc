// Task 17 remainder (authforge-sdk-refactor): unit tests for the new
// authforge::oauth2::protocol::AuthorizationService -- the first time
// this class exists in the codebase (see its header comment: the
// consent/scope decision was previously spread across ClientService +
// IdentityService + OAuth2StandardController with no single owner).

#include <authforge/common/model/Subject.h>
#include <authforge/oauth2/protocol/AuthorizationService.h>

#include <gtest/gtest.h>

#include <unordered_map>

namespace
{

using namespace authforge::oauth2::model;
using namespace authforge::oauth2::repository;
using authforge::oauth2::protocol::AuthorizationService;

class FakeClientRepo : public IClientRepository
{
  public:
    std::unordered_map<std::string, OAuth2Client> clients;

    void getClient(const std::string &clientId, ClientCallback &&cb) override
    {
        auto it = clients.find(clientId);
        cb(it == clients.end() ? std::nullopt : std::make_optional(it->second));
    }
    void validateClient(const std::string &, const std::string &, BoolCallback &&cb) override
    {
        cb(true);
    }
};

class FakeConsentRepo : public IConsentRepository
{
  public:
    // key: internalUserId:clientId:scope
    std::unordered_map<std::string, bool> consents;

    static std::string key(const UserRef &user, const std::string &clientId, const std::string &scope)
    {
        return std::to_string(user.internalUserId) + ":" + clientId + ":" + scope;
    }

    void hasUserConsent(
      const UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) override
    {
        auto it = consents.find(key(user, clientId, scope));
        cb(it != consents.end() && it->second);
    }
    void saveUserConsent(
      const UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) override
    {
        consents[key(user, clientId, scope)] = true;
        cb(true);
    }
    void revokeUserConsent(
      const UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      VoidCallback &&cb
    ) override
    {
        consents[key(user, clientId, scope)] = false;
        cb();
    }
};

class FakeSubjectResolver : public authforge::common::ports::ISubjectResolver
{
  public:
    std::unordered_map<std::string, int32_t> mapping;

    void resolve(const authforge::common::model::Subject &subject, ResolveCallback &&cb) override
    {
        auto it = mapping.find(subject.value());
        cb(it == mapping.end() ? std::nullopt : std::make_optional(it->second));
    }
};

class FakeRoleProvider : public authforge::common::ports::IRoleProvider
{
  public:
    std::unordered_map<int32_t, std::vector<std::string>> roles;

    void getRoles(int32_t internalUserId, RolesCallback &&cb) override
    {
        auto it = roles.find(internalUserId);
        cb(it == roles.end() ? std::vector<std::string>{} : it->second);
    }
};

std::shared_ptr<FakeClientRepo> makeClientRepo()
{
    auto repo = std::make_shared<FakeClientRepo>();
    OAuth2Client c;
    c.clientId = "test-client";
    c.clientType = ClientType::CONFIDENTIAL;
    c.allowedScopes = {"openid", "profile", "admin"};
    repo->clients["test-client"] = c;
    return repo;
}

}  // namespace

TEST(AuthorizationServiceTest, EvaluateScopes_UnknownClient_AllInvalid)
{
    auto clients = makeClientRepo();
    AuthorizationService svc(clients);

    authforge::oauth2::access::ScopeValidationSummary summary;
    svc.evaluateScopes(
      "unknown-client", "local:alice", {"openid"}, [&](auto s) { summary = std::move(s); }
    );

    EXPECT_TRUE(summary.hasErrors());
    ASSERT_EQ(summary.invalid.size(), 1u);
    EXPECT_EQ(summary.invalid[0], "openid");
    EXPECT_EQ(summary.invalidReasons[0], "client_not_found");
}

TEST(AuthorizationServiceTest, EvaluateScopes_ScopeNotAllowed_Invalid)
{
    auto clients = makeClientRepo();
    AuthorizationService svc(clients);

    authforge::oauth2::access::ScopeValidationSummary summary;
    svc.evaluateScopes(
      "test-client", "local:alice", {"not-allowed"}, [&](auto s) { summary = std::move(s); }
    );

    EXPECT_TRUE(summary.hasErrors());
    ASSERT_EQ(summary.invalid.size(), 1u);
    EXPECT_EQ(summary.invalid[0], "not-allowed");
}

TEST(AuthorizationServiceTest, EvaluateScopes_NoConsentPort_NonAdminScope_ConsentRequired)
{
    auto clients = makeClientRepo();
    // No consent repository / subject resolver wired -> safe default is
    // ConsentRequired for every scope that passes tiers 1-2 (cannot
    // confirm consent, so do not assume it).
    AuthorizationService svc(clients);

    authforge::oauth2::access::ScopeValidationSummary summary;
    svc.evaluateScopes(
      "test-client", "local:alice", {"openid"}, [&](auto s) { summary = std::move(s); }
    );

    EXPECT_TRUE(summary.needsConsent());
    ASSERT_EQ(summary.consentRequired.size(), 1u);
    EXPECT_EQ(summary.consentRequired[0], "openid");
}

TEST(AuthorizationServiceTest, EvaluateScopes_AdminScope_NoRoleProvider_TreatedAsNonAdmin)
{
    auto clients = makeClientRepo();
    auto consents = std::make_shared<FakeConsentRepo>();
    auto subjectResolver = std::make_shared<FakeSubjectResolver>();
    subjectResolver->mapping["local:alice"] = 42;
    // roleProvider intentionally left null.
    AuthorizationService svc(clients, consents, subjectResolver, nullptr);

    authforge::oauth2::access::ScopeValidationSummary summary;
    svc.evaluateScopes(
      "test-client", "local:alice", {"admin"}, [&](auto s) { summary = std::move(s); }
    );

    // Admin-tiered scope, no way to confirm the admin role -> Invalid
    // (matches ScopeDecisionEngine's tier-2 default: hasAdminRole=false).
    EXPECT_TRUE(summary.hasErrors());
    ASSERT_EQ(summary.invalid.size(), 1u);
    EXPECT_EQ(summary.invalid[0], "admin");
}

TEST(AuthorizationServiceTest, EvaluateScopes_AdminScope_WithAdminRole_ConsentRequired)
{
    auto clients = makeClientRepo();
    auto consents = std::make_shared<FakeConsentRepo>();
    auto subjectResolver = std::make_shared<FakeSubjectResolver>();
    subjectResolver->mapping["local:alice"] = 42;
    auto roleProvider = std::make_shared<FakeRoleProvider>();
    roleProvider->roles[42] = {"admin"};
    AuthorizationService svc(clients, consents, subjectResolver, roleProvider);

    authforge::oauth2::access::ScopeValidationSummary summary;
    svc.evaluateScopes(
      "test-client", "local:alice", {"admin"}, [&](auto s) { summary = std::move(s); }
    );

    EXPECT_TRUE(summary.needsConsent());
    ASSERT_EQ(summary.consentRequired.size(), 1u);
    EXPECT_EQ(summary.consentRequired[0], "admin");
}

TEST(AuthorizationServiceTest, EvaluateScopes_AlreadyConsented_Valid)
{
    auto clients = makeClientRepo();
    auto consents = std::make_shared<FakeConsentRepo>();
    auto subjectResolver = std::make_shared<FakeSubjectResolver>();
    subjectResolver->mapping["local:alice"] = 42;
    consents->consents[FakeConsentRepo::key(UserRef{42}, "test-client", "openid")] = true;
    AuthorizationService svc(clients, consents, subjectResolver, nullptr);

    authforge::oauth2::access::ScopeValidationSummary summary;
    svc.evaluateScopes(
      "test-client", "local:alice", {"openid"}, [&](auto s) { summary = std::move(s); }
    );

    EXPECT_TRUE(summary.canProceed());
    ASSERT_EQ(summary.valid.size(), 1u);
    EXPECT_EQ(summary.valid[0], "openid");
}
