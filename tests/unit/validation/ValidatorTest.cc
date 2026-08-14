#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <authforge/drogon/validation/RuleEngine.h>
#include <authforge/drogon/validation/RuleSet.h>
#include <vector>
#include <string>

using namespace drogon;
using namespace authforge::drogon::validation;

DROGON_TEST(Unit_P0_Validation_ClientId_AllScenarios)
{
    struct TestCase
    {
        std::string clientId;
        bool shouldBeValid;
    };

    std::vector<TestCase> testCases =
      {{"my-client_123.app", true},
       {"client-1", true},
       {"invalid@client!", false},
       {"", false},
       {std::string(100, 'a'), true}};

    for (const auto &tc : testCases)
    {
        auto result = RuleEngine::validateClientId(tc.clientId);
        CHECK(result.ok == tc.shouldBeValid);
    }
}

DROGON_TEST(Unit_P0_Validation_ClientSecret_AllScenarios)
{
    struct TestCase
    {
        std::string secret;
        bool shouldBeValid;
    };

    std::vector<TestCase> testCases =
      {{"my-secret-key-123", true},
       {"ComplexP@ssw0rd!", true},
       {"short", false},
       {"", false},
       {std::string(200, 'a'), true}};

    for (const auto &tc : testCases)
    {
        auto result = RuleEngine::validateClientSecret(tc.secret);
        CHECK(result.ok == tc.shouldBeValid);
    }
}

// B1 (OIDC Back-Channel Logout 1.0 §2.3): backchannel_logout_uri scheme
// policy. Unlike redirect_uri (RuleEngine::validateRedirectUri), loopback
// http is NOT exempt — this is server-to-server delivery. The
// auth.allow_http_redirect_uri dev hatch IS honored (the test config.json
// enables it, mirroring the dev environment), so http expectations are read
// from the live config instead of hardcoded.
namespace
{
// The test env's config.json enables auth.allow_http_redirect_uri (dev
// override), so http URIs are accepted there by design. Read the live
// switch instead of hardcoding expectations.
bool backchannelTestAllowHttpOverride()
{
    auto cfg = ::drogon::app().getCustomConfig();  // by value (RuleEngine.cc pattern)
    return cfg.isMember("auth") && cfg["auth"].isMember("allow_http_redirect_uri") &&
           cfg["auth"]["allow_http_redirect_uri"].asBool();
}
}  // namespace

DROGON_TEST(Unit_P0_Validation_BackchannelLogoutUri_AllScenarios)
{
    const bool allowHttp = backchannelTestAllowHttpOverride();

    struct TestCase
    {
        std::string uri;
        bool shouldBeValid;
    };

    std::vector<TestCase> testCases =
      {{"https://rp.example.com/backchannel-logout", true},
       {"", true},  // empty == "not configured"
       {"http://rp.example.com/backchannel-logout", allowHttp},
       // loopback NOT exempt: still gated by the same override, never free
       {"http://127.0.0.1:9000/backchannel-logout", allowHttp},
       {"http://[::1]/bc", allowHttp},
       {"ftp://rp.example.com", false},
       {"rp.example.com/backchannel", false}};

    for (const auto &tc : testCases)
    {
        auto result = RuleSet::validateBackchannelLogoutUri(tc.uri);
        CHECK(result.has_value() != tc.shouldBeValid);
    }
}

DROGON_TEST(Unit_P0_Validation_RedirectUri_AllScenarios)
{
    struct TestCase
    {
        std::string uri;
        bool shouldBeValid;
    };

    std::vector<TestCase> testCases =
      {{"https://example.com/callback", true},
       {"http://localhost:3000/auth", true},
       {"ftp://invalid.com", false},
       {"not-a-url", false},
       {"", false}};

    for (const auto &tc : testCases)
    {
        auto result = RuleEngine::validateRedirectUri(tc.uri);
        CHECK(result.ok == tc.shouldBeValid);
    }
}

DROGON_TEST(Unit_P0_Validation_Token_AllScenarios)
{
    struct TestCase
    {
        std::string token;
        bool shouldBeValid;
    };

    std::vector<TestCase> testCases =
      {{"abcdefghijklmnopqrstuvwxyz123456", true}, {"too-short", false}, {"", false}};

    for (const auto &tc : testCases)
    {
        auto result = RuleEngine::validateToken(tc.token);
        CHECK(result.ok == tc.shouldBeValid);
    }
}

DROGON_TEST(Unit_P0_Validation_Scope_AllScenarios)
{
    struct TestCase
    {
        std::string scope;
        bool shouldBeValid;
    };

    std::vector<TestCase> testCases =
      {{"read write", true},
       {"profile:read email:write", true},
       {"", false},
       {"invalid@scope!", false}};

    for (const auto &tc : testCases)
    {
        auto result = RuleEngine::validateScope(tc.scope);
        CHECK(result.ok == tc.shouldBeValid);
    }
}

DROGON_TEST(Unit_P0_Validation_OAuthTypes_AllScenarios)
{
    CHECK(RuleEngine::validateResponseType("code").ok);
    CHECK(RuleEngine::validateResponseType("token").ok);
    CHECK(!RuleEngine::validateResponseType("invalid").ok);

    CHECK(RuleEngine::validateGrantType("authorization_code").ok);
    CHECK(RuleEngine::validateGrantType("refresh_token").ok);
    CHECK(RuleEngine::validateGrantType("client_credentials").ok);
    CHECK(!RuleEngine::validateGrantType("invalid_grant").ok);
}

DROGON_TEST(Unit_P1_Validation_Primitives_BasicRules)
{
    CHECK(RuleEngine::notEmpty("valid", "field").ok);
    CHECK(!RuleEngine::notEmpty("", "field").ok);

    CHECK(RuleEngine::length("valid", "field", 3, 10).ok);
    CHECK(!RuleEngine::length("ab", "field", 3, 10).ok);
    CHECK(!RuleEngine::length("this_is_way_too_long", "field", 3, 10).ok);

    CHECK(RuleEngine::regex("test123", "field", "^[a-z0-9]+$").ok);
    CHECK(!RuleEngine::regex("test@123", "field", "^[a-z0-9]+$").ok);

    CHECK(RuleEngine::numericRange(5, "field", 1, 10).ok);
    CHECK(!RuleEngine::numericRange(15, "field", 1, 10).ok);
}
