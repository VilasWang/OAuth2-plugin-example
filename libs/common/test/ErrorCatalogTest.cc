// Task 13 (authforge-sdk-refactor, design.md §6): pure gtest unit tests for
// authforge::common::error::ErrorCatalog / Error. No DB/no Drogon.
//
// These tests exercise the framework-agnostic port only; they do not
// re-verify every property the pre-existing oauth2::error test suite
// (OAuth2Server/test/unit/error/ErrorCatalogPropertyTest.cc) already covers
// for the Drogon-dependent original -- that suite continues to test the
// original OAuth2Plugin/include/oauth2/error/* headers, which Task 13 does
// not touch. This suite's job is to prove the PORTED copy behaves
// identically for the invariants that matter to a Domain-layer consumer.

#include <authforge/common/error/ErrorCatalog.h>
#include <authforge/common/error/ErrorTypes.h>

#include <gtest/gtest.h>

using namespace authforge::common::error;

TEST(ErrorCatalogTest, ValidateInvariantsDoesNotThrow)
{
    EXPECT_NO_THROW(ErrorCatalog::validateInvariants());
}

TEST(ErrorCatalogTest, InternalErrorEntryExists)
{
    const CatalogEntry &entry = ErrorCatalog::internalError();
    EXPECT_EQ(entry.code, "INTERNAL_ERROR");
    EXPECT_EQ(entry.numericCode, 6001);
}

TEST(ErrorCatalogTest, FindReturnsRegisteredEntry)
{
    const CatalogEntry *entry = ErrorCatalog::find("AUTH_INVALID_CREDENTIALS");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->category, ErrorCategory::AUTHENTICATION);
    EXPECT_EQ(entry->httpStatus, 401);
}

TEST(ErrorCatalogTest, FindReturnsNullForUnknownCode)
{
    EXPECT_EQ(ErrorCatalog::find("NO_SUCH_CODE"), nullptr);
}

TEST(ErrorCatalogTest, ResourceNotFoundOverridesTo404)
{
    const CatalogEntry *entry = ErrorCatalog::find("VALIDATION_RESOURCE_NOT_FOUND");
    ASSERT_NE(entry, nullptr);
    // VALIDATION category defaults to 400, but this entry overrides to 404.
    EXPECT_EQ(entry->httpStatus, 404);
}

TEST(ErrorCatalogTest, FindOAuthReturnsRegisteredEntry)
{
    const OAuthCatalogEntry *entry = ErrorCatalog::findOAuth("invalid_grant");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->httpStatus, 400);
}

TEST(ErrorTest, FromCodePopulatesCategoryAndDefaultMessage)
{
    Error e = Error::fromCode("AUTH_TOKEN_EXPIRED", "req-42");
    EXPECT_EQ(e.code, "AUTH_TOKEN_EXPIRED");
    EXPECT_EQ(e.category, ErrorCategory::AUTHENTICATION);
    EXPECT_FALSE(e.message.empty());
    EXPECT_EQ(e.requestId, "req-42");
}

TEST(ErrorTest, FromCodeFallsBackToInternalErrorForUnknownCode)
{
    Error e = Error::fromCode("NO_SUCH_CODE", "req-1");
    EXPECT_EQ(e.code, "INTERNAL_ERROR");
    EXPECT_EQ(e.category, ErrorCategory::INTERNAL);
}

TEST(ErrorTest, ToHttpStatusCodeMatchesCatalog)
{
    Error e = Error::fromCode("AUTHZ_ACCESS_DENIED", "req-1");
    EXPECT_EQ(e.toHttpStatusCode(), 403);
}

TEST(ErrorTest, ToJsonOmitsDetailsWhenNotIncluded)
{
    Error e = Error::fromCode("VALIDATION_INVALID_INPUT", "req-1");
    e.details = "some internal detail";
    Json::Value json = e.toJson(/*includeDetails=*/false);
    EXPECT_FALSE(json["error"].isMember("details"));
}

TEST(ErrorTest, ToJsonIncludesDetailsWhenRequested)
{
    Error e = Error::fromCode("VALIDATION_INVALID_INPUT", "req-1");
    e.details = "some internal detail";
    Json::Value json = e.toJson(/*includeDetails=*/true);
    ASSERT_TRUE(json["error"].isMember("details"));
    EXPECT_EQ(json["error"]["details"].asString(), "some internal detail");
}

TEST(ErrorTest, ToJsonIncludesNumericCodeForRegisteredEntry)
{
    Error e = Error::fromCode("DB_QUERY_ERROR", "req-1");
    Json::Value json = e.toJson(/*includeDetails=*/false);
    ASSERT_TRUE(json["error"].isMember("numeric_code"));
    EXPECT_EQ(json["error"]["numeric_code"].asInt(), 2002);
}

TEST(ErrorTest, FromExceptionCapturesWhatAsDetails)
{
    std::runtime_error ex("boom");
    Error e = Error::fromException(ex, ErrorCategory::DATABASE, "req-1");
    EXPECT_EQ(e.details, "boom");
    EXPECT_EQ(e.category, ErrorCategory::DATABASE);
}

TEST(ErrorTest, FromExceptionUnmappedHintFallsBackToInternal)
{
    std::runtime_error ex("whatever");
    Error e = Error::fromException(ex, ErrorCategory::UNKNOWN, "req-1");
    EXPECT_EQ(e.code, "INTERNAL_ERROR");
}
