// Coverage additions (P1, authforge coverage push): RuleSet::validateField is
// the core rule evaluator with 5 branches (required+empty, optional+empty,
// minLength, maxLength, pattern, custom) and had ZERO direct unit coverage
// -- it was exercised only indirectly through the composite oauth2/login/
// register validators. These pin each branch in isolation.

#include <drogon/drogon_test.h>
#include <authforge/drogon/validation/RuleSet.h>

using authforge::drogon::validation::Rule;
using authforge::drogon::validation::RuleSet;

namespace
{
Rule makeRule(const std::string &field)
{
    Rule r;
    r.field = field;
    r.source = "body";
    r.required = false;
    return r;
}
}  // namespace

DROGON_TEST(Unit_P1_Validation_RuleSet_ValidateField_RequiredAndEmpty_ReturnsRequiredError)
{
    Rule r = makeRule("username");
    r.required = true;
    auto err = RuleSet::validateField("", "username", r);
    REQUIRE(err.has_value());
    CHECK(err->find("required") != std::string::npos);
}

DROGON_TEST(Unit_P1_Validation_RuleSet_ValidateField_OptionalAndEmpty_ReturnsNullopt)
{
    Rule r = makeRule("nickname");
    // optional + empty -> skip all further validation.
    auto err = RuleSet::validateField("", "nickname", r);
    CHECK(!err.has_value());
}

DROGON_TEST(Unit_P1_Validation_RuleSet_ValidateField_BelowMinLength_ReturnsError)
{
    Rule r = makeRule("password");
    r.minLength = 8;
    auto err = RuleSet::validateField("short", "password", r);
    REQUIRE(err.has_value());
    CHECK(err->find("at least 8") != std::string::npos);
}

DROGON_TEST(Unit_P1_Validation_RuleSet_ValidateField_AboveMaxLength_ReturnsError)
{
    Rule r = makeRule("bio");
    r.maxLength = 5;
    auto err = RuleSet::validateField("way-too-long-value", "bio", r);
    REQUIRE(err.has_value());
    CHECK(err->find("at most 5") != std::string::npos);
}

DROGON_TEST(Unit_P1_Validation_RuleSet_ValidateField_PatternMismatch_ReturnsFormatError)
{
    Rule r = makeRule("email");
    r.pattern = "^[^@]+@[^@]+$";
    auto err = RuleSet::validateField("not-an-email", "email", r);
    REQUIRE(err.has_value());
    CHECK(err->find("format is invalid") != std::string::npos);
}

DROGON_TEST(Unit_P1_Validation_RuleSet_ValidateField_PatternMatch_ReturnsNullopt)
{
    Rule r = makeRule("email");
    r.pattern = "^[^@]+@[^@]+$";
    auto err = RuleSet::validateField("a@b.test", "email", r);
    CHECK(!err.has_value());
}

DROGON_TEST(Unit_P2_Validation_RuleSet_ValidateField_InvalidRegexPattern_DoesNotThrow)
{
    Rule r = makeRule("code");
    r.pattern = "[invalid(unbalanced";  // malformed regex
    // The catch(std::regex_error) swallows the failure and does NOT block
    // validation (RuleSet.cc:50-54). Value is accepted (nullopt).
    auto err = RuleSet::validateField("anything", "code", r);
    CHECK(!err.has_value());
}

DROGON_TEST(Unit_P1_Validation_RuleSet_ValidateField_CustomValidator_Fails)
{
    Rule r = makeRule("token");
    r.custom = [](const std::string &v) { return v == "magic"; };
    auto err = RuleSet::validateField("not-magic", "token", r);
    REQUIRE(err.has_value());
    CHECK(err->find("validation failed") != std::string::npos);
}

DROGON_TEST(Unit_P1_Validation_RuleSet_ValidateField_CustomValidator_Passes)
{
    Rule r = makeRule("token");
    r.custom = [](const std::string &v) { return v == "magic"; };
    auto err = RuleSet::validateField("magic", "token", r);
    CHECK(!err.has_value());
}

DROGON_TEST(Unit_P1_Validation_RuleSet_ValidateField_AllConstraintsPass_ReturnsNullopt)
{
    Rule r = makeRule("username");
    r.required = true;
    r.minLength = 3;
    r.maxLength = 20;
    r.pattern = "^[a-z]+$";
    r.custom = [](const std::string &v) { return v.size() >= 3; };
    auto err = RuleSet::validateField("alice", "username", r);
    CHECK(!err.has_value());
}
