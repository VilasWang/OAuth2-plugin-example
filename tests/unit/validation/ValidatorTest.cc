#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <fulla/drogon/validation/RuleEngine.h>
#include <fulla/drogon/validation/RuleSet.h>
#include <vector>
#include <string>

using namespace drogon;
using namespace fulla::drogon::validation;

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
        std::string label;
    };

    std::vector<TestCase> testCases =
      {{"https://rp.example.com/backchannel-logout", true, "plain https"},
       {"https://rp.example.com:8443/bc", true, "https with port"},
       {"", true, "empty == not configured"},
       {"http://rp.example.com/backchannel-logout", allowHttp, "http under dev hatch"},
       // Private hosts are rejected by the SEPARATE allow_private flag (unset
       // in the test config), even when the http dev hatch is on.
       {"http://127.0.0.1:9000/backchannel-logout", false, "http loopback: private beats hatch"},
       {"http://[::1]/bc", false, "http v6 loopback: private beats hatch"},
       {"ftp://rp.example.com", false, "ftp scheme"},
       {"rp.example.com/backchannel", false, "no scheme"},
       // #57 structure checks
       {"https://", false, "empty authority"},
       {"https:///backchannel-logout", false, "empty host"},
       {"https://:8443/bc", false, "port-only authority"},
       {"https://user:pass@rp.example.com/bc", false, "userinfo"},
       {"https://rp.example.com/bc#frag", false, "fragment"},
       {"https://[::1/bc", false, "unterminated IPv6 bracket"},
       // #57 private/loopback rejection (https does NOT bypass it)
       {"https://localhost/bc", false, "localhost"},
       {"https://127.0.0.1/bc", false, "v4 loopback"},
       {"https://10.1.2.3/bc", false, "10/8"},
       {"https://172.16.0.9/bc", false, "172.16/12"},
       {"https://192.168.1.1/bc", false, "192.168/16"},
       {"https://169.254.169.254/bc", false, "169.254/16 metadata"},
       {"https://0.0.0.0/bc", false, "0/8"},
       {"https://[::1]/bc", false, "v6 loopback"},
       {"https://[fd00::1]/bc", false, "fc00::/7"},
       {"https://[fe80::1]/bc", false, "fe80::/10"},
       {"https://[::ffff:127.0.0.1]/bc", false, "v4-mapped loopback"},
       {"https://172.32.0.1/bc", true, "172.32 is PUBLIC (outside 172.16/12)"},
       // #57 length cap (column is VARCHAR(512))
       {"https://rp.example.com/" + std::string(600, 'a'), false, "> 512 chars"}};

    for (const auto &tc : testCases)
    {
        auto result = RuleSet::validateBackchannelLogoutUri(tc.uri);
        if (result.has_value() == tc.shouldBeValid)
        {
            FAULT("backchannel uri case '" + tc.label + "' misclassified: " +
                  (result ? *result : std::string("accepted")));
        }
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
