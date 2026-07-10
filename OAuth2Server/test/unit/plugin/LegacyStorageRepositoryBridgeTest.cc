// M3 Task 24 slice 1 (authforge-sdk-refactor, PROGRESS.md "Task 24 切分
// 方案"): unit tests for oauth2::adapters::{Client,Grant,Token,Consent}
// RepositoryBridge -- verifies that going through the bridge produces the
// SAME results as calling the underlying MemoryOAuth2Storage directly, so
// the bridge is proven to be a pure, behavior-preserving forwarding
// adapter before it is wired into the new TokenService/ClientService
// (Task 24 slice 2).

#include <drogon/drogon_test.h>
#include <oauth2/adapters/LegacyStorageRepositoryBridge.h>
#include <oauth2/storage/MemoryOAuth2Storage.h>

#include <authforge/oauth2/model/Client.h>

using namespace oauth2::adapters;
using authforge::oauth2::model::ClientType;

namespace
{
std::shared_ptr<oauth2::MemoryOAuth2Storage> makeSeededStorage()
{
    auto storage = std::make_shared<oauth2::MemoryOAuth2Storage>();

    Json::Value clients;
    Json::Value c;
    c["type"] = "CONFIDENTIAL";
    c["secret"] = "secret";
    c["redirect_uri"] = "https://example.test/cb";
    Json::Value scopes(Json::arrayValue);
    scopes.append("openid");
    scopes.append("profile");
    c["allowed_scopes"] = scopes;
    clients["test-client"] = c;

    storage->initFromConfig(clients, Json::Value::nullSingleton());
    return storage;
}
}  // namespace

DROGON_TEST(Unit_LegacyBridge_ClientRepository_GetClient_MatchesDirectStorageCall)
{
    auto storage = makeSeededStorage();
    ClientRepositoryBridge bridge(storage);

    std::optional<::oauth2::OAuth2Client> direct;
    storage->getClient("test-client", [&](std::optional<::oauth2::OAuth2Client> c) { direct = c; });

    std::optional<authforge::oauth2::model::OAuth2Client> viaBridge;
    bridge.getClient("test-client", [&](std::optional<authforge::oauth2::model::OAuth2Client> c) {
        viaBridge = c;
    });

    REQUIRE(direct.has_value());
    REQUIRE(viaBridge.has_value());
    CHECK(viaBridge->clientId == direct->clientId);
    CHECK(viaBridge->clientSecretHash == direct->clientSecretHash);
    CHECK(viaBridge->redirectUris == direct->redirectUris);
    CHECK(viaBridge->allowedScopes == direct->allowedScopes);
    CHECK((viaBridge->clientType == ClientType::CONFIDENTIAL));
}

DROGON_TEST(Unit_LegacyBridge_ClientRepository_GetClient_UnknownReturnsNullopt)
{
    auto storage = makeSeededStorage();
    ClientRepositoryBridge bridge(storage);

    std::optional<authforge::oauth2::model::OAuth2Client> result;
    bool called = false;
    bridge.getClient("nonexistent", [&](auto c) {
        result = c;
        called = true;
    });

    REQUIRE(called);
    CHECK(!result.has_value());
}

DROGON_TEST(Unit_LegacyBridge_ClientRepository_ValidateClient_ForwardsCorrectly)
{
    auto storage = makeSeededStorage();
    ClientRepositoryBridge bridge(storage);

    bool valid = false;
    bridge.validateClient("test-client", "secret", [&](bool v) { valid = v; });
    CHECK(valid);

    bool invalid = true;
    bridge.validateClient("test-client", "wrong-secret", [&](bool v) { invalid = v; });
    CHECK(!invalid);
}

DROGON_TEST(Unit_LegacyBridge_GrantRepository_AuthCodeRoundTrip_MatchesDirectStorage)
{
    auto storage = makeSeededStorage();
    GrantRepositoryBridge bridge(storage);

    authforge::oauth2::model::OAuth2AuthCode code;
    code.code = "test-code-hash";
    code.clientId = "test-client";
    code.userId = "alice";
    code.scope = "openid profile";
    code.redirectUri = "https://example.test/cb";
    code.expiresAt = 9999999999;

    bool saved = false;
    bridge.saveAuthCode(code, [&]() { saved = true; });
    REQUIRE(saved);

    std::optional<authforge::oauth2::model::OAuth2AuthCode> fetched;
    bridge.getAuthCode("test-code-hash", [&](auto c) { fetched = c; });
    REQUIRE(fetched.has_value());
    CHECK(fetched->clientId == "test-client");
    CHECK(fetched->userId == "alice");
    CHECK(fetched->scope == "openid profile");

    // Confirm this is the SAME record the underlying storage sees directly.
    std::optional<::oauth2::OAuth2AuthCode> direct;
    storage->getAuthCode("test-code-hash", [&](auto c) { direct = c; });
    REQUIRE(direct.has_value());
    CHECK(direct->clientId == fetched->clientId);
}

DROGON_TEST(Unit_LegacyBridge_GrantRepository_ConsumeAuthCode_RedirectUriMismatchFails)
{
    auto storage = makeSeededStorage();
    GrantRepositoryBridge bridge(storage);

    authforge::oauth2::model::OAuth2AuthCode code;
    code.code = "code-2";
    code.clientId = "test-client";
    code.userId = "alice";
    code.redirectUri = "https://example.test/cb";
    code.expiresAt = 9999999999;
    bridge.saveAuthCode(code, []() {});

    std::optional<authforge::oauth2::model::OAuth2AuthCode> wrongUri;
    bridge.consumeAuthCode("code-2", "https://evil.test/cb", [&](auto c) { wrongUri = c; });
    CHECK(!wrongUri.has_value());

    std::optional<authforge::oauth2::model::OAuth2AuthCode> correctUri;
    bridge.consumeAuthCode("code-2", "https://example.test/cb", [&](auto c) { correctUri = c; });
    CHECK(correctUri.has_value());

    // Single-use: a second consume attempt must fail even with the right URI.
    std::optional<authforge::oauth2::model::OAuth2AuthCode> reused;
    bridge.consumeAuthCode("code-2", "https://example.test/cb", [&](auto c) { reused = c; });
    CHECK(!reused.has_value());
}

DROGON_TEST(Unit_LegacyBridge_TokenRepository_SaveTokenPairAndRetrieve_RoundTrips)
{
    auto storage = makeSeededStorage();
    TokenRepositoryBridge bridge(storage, "memory");

    authforge::oauth2::model::OAuth2AccessToken at;
    at.token = "access-1";
    at.clientId = "test-client";
    at.userId = "alice";
    at.scope = "openid";
    at.expiresAt = 9999999999;

    authforge::oauth2::model::OAuth2RefreshToken rt;
    rt.token = "refresh-1";
    rt.accessToken = "access-1";
    rt.clientId = "test-client";
    rt.userId = "alice";
    rt.scope = "openid";
    rt.expiresAt = 9999999999;
    rt.familyId = "family-1";

    bool saved = false;
    bridge.saveTokenPair(at, rt, [&]() { saved = true; });
    REQUIRE(saved);

    std::optional<authforge::oauth2::model::OAuth2AccessToken> fetchedAt;
    bridge.getAccessToken("access-1", [&](auto t) { fetchedAt = t; });
    REQUIRE(fetchedAt.has_value());
    CHECK(fetchedAt->userId == "alice");

    std::optional<authforge::oauth2::model::OAuth2RefreshToken> fetchedRt;
    bridge.getRefreshToken("refresh-1", [&](auto t) { fetchedRt = t; });
    REQUIRE(fetchedRt.has_value());
    CHECK(fetchedRt->familyId == "family-1");
}

DROGON_TEST(Unit_LegacyBridge_TokenRepository_AtomicRevokeRefreshToken_CasSemantics)
{
    auto storage = makeSeededStorage();
    TokenRepositoryBridge bridge(storage, "memory");

    authforge::oauth2::model::OAuth2AccessToken at;
    at.token = "access-2";
    at.clientId = "test-client";
    at.userId = "alice";
    at.expiresAt = 9999999999;
    authforge::oauth2::model::OAuth2RefreshToken rt;
    rt.token = "refresh-2";
    rt.accessToken = "access-2";
    rt.clientId = "test-client";
    rt.userId = "alice";
    rt.expiresAt = 9999999999;
    rt.familyId = "family-2";
    bridge.saveTokenPair(at, rt, []() {});

    std::optional<authforge::oauth2::model::OAuth2RefreshToken> firstRevoke;
    bridge.atomicRevokeRefreshToken("refresh-2", [&](auto t) { firstRevoke = t; });
    CHECK(firstRevoke.has_value());

    std::optional<authforge::oauth2::model::OAuth2RefreshToken> secondRevoke;
    bridge.atomicRevokeRefreshToken("refresh-2", [&](auto t) { secondRevoke = t; });
    CHECK(!secondRevoke.has_value());
}

DROGON_TEST(Unit_LegacyBridge_TokenRepository_CapabilityFlags_ReflectStorageType)
{
    auto storage = makeSeededStorage();

    TokenRepositoryBridge memoryBridge(storage, "memory");
    CHECK(!memoryBridge.supportsTransactions());
    CHECK(memoryBridge.supportsCas());

    TokenRepositoryBridge postgresBridge(storage, "postgres");
    CHECK(postgresBridge.supportsTransactions());
    CHECK(postgresBridge.supportsCas());
}

DROGON_TEST(Unit_LegacyBridge_TokenRepository_IntrospectToken_ActiveAndInactive)
{
    auto storage = makeSeededStorage();
    TokenRepositoryBridge bridge(storage, "memory");

    authforge::oauth2::model::OAuth2AccessToken at;
    at.token = "access-3";
    at.clientId = "test-client";
    at.userId = "alice";
    at.scope = "openid";
    at.expiresAt = 9999999999;
    bridge.saveAccessToken(at, []() {});

    std::optional<authforge::oauth2::model::TokenIntrospection> active;
    bridge.introspectToken("access-3", [&](auto i) { active = i; });
    REQUIRE(active.has_value());
    CHECK(active->active);
    CHECK(active->clientId == "test-client");

    std::optional<authforge::oauth2::model::TokenIntrospection> inactive;
    bridge.introspectToken("unknown-token", [&](auto i) { inactive = i; });
    REQUIRE(inactive.has_value());
    CHECK(!inactive->active);
}

DROGON_TEST(Unit_LegacyBridge_ConsentRepository_SaveHasRevoke_RoundTrips)
{
    auto storage = makeSeededStorage();
    ConsentRepositoryBridge bridge(storage);

    authforge::oauth2::model::UserRef user{42};

    bool before = true;
    bridge.hasUserConsent(user, "test-client", "openid", [&](bool v) { before = v; });
    CHECK(!before);

    bool saved = false;
    bridge.saveUserConsent(user, "test-client", "openid", [&](bool ok) { saved = ok; });
    CHECK(saved);

    bool after = false;
    bridge.hasUserConsent(user, "test-client", "openid", [&](bool v) { after = v; });
    CHECK(after);

    bool revoked = false;
    bridge.revokeUserConsent(user, "test-client", "openid", [&]() { revoked = true; });
    CHECK(revoked);

    bool afterRevoke = true;
    bridge.hasUserConsent(user, "test-client", "openid", [&](bool v) { afterRevoke = v; });
    CHECK(!afterRevoke);
}
