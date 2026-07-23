#include <drogon/drogon_test.h>
#include <oauth2/validation/RuleEngine.h>
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
