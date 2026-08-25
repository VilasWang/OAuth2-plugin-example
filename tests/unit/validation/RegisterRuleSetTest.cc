#include <drogon/drogon_test.h>
#include <drogon/HttpRequest.h>
#include <fulla/drogon/validation/RuleSet.h>

using fulla::drogon::validation::RuleSet;
using drogon::HttpRequest;
using drogon::HttpRequestPtr;

// Helper: build a form-urlencoded POST so RuleSet parses via getParameters()
static HttpRequestPtr makeRegisterRequest(
  const std::string &username,
  const std::string &password,
  const std::string &email
)
{
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath("/api/register");
    std::string body = "username=" + username + "&password=" + password + "&email=" + email;
    req->setBody(body);
    req->addHeader("Content-Type", "application/x-www-form-urlencoded");
    return req;
}

DROGON_TEST(Unit_P2_RegisterRuleSet_RejectsAtSignInUsername)
{
    auto req = makeRegisterRequest("user@name", "Password123", "user@example.com");
    auto errors = RuleSet::registerUser(req);
    bool hasFormatError = false;
    for (const auto &e : errors)
        if (e.find("username format") != std::string::npos)
            hasFormatError = true;
    CHECK(hasFormatError == true);
}

DROGON_TEST(Unit_P2_RegisterRuleSet_AcceptsValidUsername)
{
    auto req = makeRegisterRequest("alice_99", "Password123", "alice@example.com");
    auto errors = RuleSet::registerUser(req);
    bool hasUsernameError = false;
    for (const auto &e : errors)
        if (e.find("username") != std::string::npos)
            hasUsernameError = true;
    CHECK(hasUsernameError == false);
}
