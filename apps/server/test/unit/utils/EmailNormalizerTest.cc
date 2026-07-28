#include <drogon/drogon_test.h>
#include <authforge/common/utils/EmailNormalizer.h>
#include <authforge/drogon/validation/Rule.h>
#include <regex>
#include <string>

using authforge::common::utils::normalizeEmail;
using authforge::drogon::validation::EMAIL_MAX_LEN;
using authforge::drogon::validation::EMAIL_PATTERN;

// Helper: check whether an address passes the W3C HTML5 email regex.
static bool matchesEmailPattern(const std::string &email)
{
    try
    {
        std::regex re(EMAIL_PATTERN);
        return std::regex_match(email, re);
    }
    catch (const std::regex_error &)
    {
        return false;
    }
}

// ========== EMAIL_PATTERN: valid addresses MUST match ==========

DROGON_TEST(Unit_P0_Validation_Email_Pattern_AcceptsValid)
{
    CHECK(matchesEmailPattern("user@example.com"));
    CHECK(matchesEmailPattern("user.name@sub.example.com"));
    CHECK(matchesEmailPattern("user+tag@gmail.com"));
    CHECK(matchesEmailPattern("user@domain.co.uk"));
    CHECK(matchesEmailPattern("a@b.cc"));
    CHECK(matchesEmailPattern("USER@EXAMPLE.COM"));  // case-insensitive local
    CHECK(matchesEmailPattern("user-name@my-domain.org"));
}

// ========== EMAIL_PATTERN: invalid addresses MUST NOT match ==========

DROGON_TEST(Unit_P0_Validation_Email_Pattern_RejectsInvalid)
{
    CHECK(!matchesEmailPattern("plainaddress"));      // no @
    CHECK(!matchesEmailPattern("@no-local.com"));     // no local part
    CHECK(!matchesEmailPattern("user@.com"));         // domain starts with dot
    CHECK(!matchesEmailPattern("user@domain..com"));  // consecutive dots in domain
    CHECK(!matchesEmailPattern("user name@x.com"));   // embedded space
    CHECK(!matchesEmailPattern("user@"));             // no domain
}

// ========== EMAIL_PATTERN: length guard constant ==========

DROGON_TEST(Unit_P0_Validation_Email_MaxLen_IsRFC5321)
{
    CHECK(EMAIL_MAX_LEN == 254);
}

// ========== normalizeEmail: Gmail alias folding ==========

DROGON_TEST(Unit_P1_Utils_EmailNormalizer_GmailFoldsDotsAndPlus)
{
    // Dots and +tags are insignificant in Gmail local parts.
    CHECK(normalizeEmail("u.s.er@gmail.com") == "user@gmail.com");
    CHECK(normalizeEmail("user+tag@gmail.com") == "user@gmail.com");
    CHECK(normalizeEmail("U.S.Er+Newsletter@gmail.com") == "user@gmail.com");
    // googlemail.com is the same mailbox as gmail.com after folding.
    CHECK(normalizeEmail("user.x@googlemail.com") == "userx@googlemail.com");
    // Equivalence: an alias and its canonical form MUST fold to the same key.
    // This is the invariant password-reset lookup (Task 3) and admin write
    // (Task 5) rely on — a plus/dot/case variant resolves to the stored row.
    CHECK(normalizeEmail("User.Tag+promo@gmail.com") == normalizeEmail("usertag@gmail.com"));
}

DROGON_TEST(Unit_P1_Utils_EmailNormalizer_NonGmailUnchanged)
{
    // Non-Gmail providers keep their structure (only trimmed + lowercased).
    CHECK(normalizeEmail("User.Name+tag@Outlook.com") == "user.name+tag@outlook.com");
    CHECK(normalizeEmail("a.b@company.co.uk") == "a.b@company.co.uk");
}

DROGON_TEST(Unit_P1_Utils_EmailNormalizer_TrimsAndLowercases)
{
    CHECK(normalizeEmail("  User@Example.COM  ") == "user@example.com");
}

DROGON_TEST(Unit_P1_Utils_EmailNormalizer_NoAtReturnsLowercased)
{
    // Malformed input (no '@') is returned trimmed + lowercased unchanged.
    // Format validation is the caller's responsibility (RuleSet).
    CHECK(normalizeEmail("  NotAnEmail  ") == "notanemail");
}

DROGON_TEST(Unit_P1_Utils_EmailNormalizer_EmptyStaysEmpty)
{
    CHECK(normalizeEmail("").empty());
    CHECK(normalizeEmail("   ").empty());
}
