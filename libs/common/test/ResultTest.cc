// Task 13 (authforge-sdk-refactor, design.md §6): pure gtest unit tests for
// authforge::common::result::Result<T,E>. No DB/no Drogon.

#include <authforge/common/result/Result.h>
#include <authforge/common/error/ErrorTypes.h>

#include <gtest/gtest.h>

#include <string>

using authforge::common::error::Error;
using authforge::common::error::ErrorCategory;
using authforge::common::result::Result;

TEST(ResultTest, OkHoldsValue)
{
    auto r = Result<int>::ok(42);
    EXPECT_TRUE(r.ok());
    EXPECT_FALSE(r.isError());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultTest, ErrHoldsError)
{
    Error e{"TEST_CODE", ErrorCategory::VALIDATION, "test message", "", "req-1"};
    auto r = Result<int>::err(e);
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(r.isError());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error().code, "TEST_CODE");
}

TEST(ResultTest, ValueOnErrorThrows)
{
    Error e{"TEST_CODE", ErrorCategory::VALIDATION, "test message", "", "req-1"};
    auto r = Result<int>::err(e);
    EXPECT_THROW(r.value(), authforge::common::result::BadResultAccess);
}

TEST(ResultTest, ErrorOnOkThrows)
{
    auto r = Result<int>::ok(1);
    EXPECT_THROW(r.error(), authforge::common::result::BadResultAccess);
}

TEST(ResultTest, ValueOrReturnsFallbackOnError)
{
    Error e{"TEST_CODE", ErrorCategory::VALIDATION, "test message", "", "req-1"};
    auto r = Result<int>::err(e);
    EXPECT_EQ(r.valueOr(99), 99);
}

TEST(ResultTest, ValueOrReturnsValueOnOk)
{
    auto r = Result<int>::ok(7);
    EXPECT_EQ(r.valueOr(99), 7);
}

TEST(ResultTest, WorksWithNonPrimitiveT)
{
    auto r = Result<std::string>::ok(std::string("hello"));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.value(), "hello");
}

// Custom (non-Error) E parameter: Result is not hard-coded to
// authforge::common::error::Error.
enum class LocalErrorCode
{
    NotFound,
    Invalid,
};

TEST(ResultTest, WorksWithCustomErrorType)
{
    auto r = Result<int, LocalErrorCode>::err(LocalErrorCode::NotFound);
    EXPECT_TRUE(r.isError());
    EXPECT_EQ(r.error(), LocalErrorCode::NotFound);
}
