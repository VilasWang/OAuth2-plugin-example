// Task 15 (authforge-sdk-refactor, design.md §6/§8): pure gtest unit tests
// for FakeEmailSender.

#include <authforge/common/testing/FakeEmailSender.h>

#include <gtest/gtest.h>

using namespace authforge::common::testing;

TEST(FakeEmailSenderTest, CapturesSentMessage)
{
    FakeEmailSender sender;
    bool callbackResult = false;
    bool callbackInvoked = false;

    sender.sendEmail("alice@example.com", "Reset your password", "Click here", [&](bool ok) {
        callbackInvoked = true;
        callbackResult = ok;
    });

    ASSERT_EQ(sender.count(), 1u);
    EXPECT_EQ(sender.sent()[0].to, "alice@example.com");
    EXPECT_EQ(sender.sent()[0].subject, "Reset your password");
    EXPECT_EQ(sender.sent()[0].body, "Click here");
    EXPECT_TRUE(callbackInvoked);
    EXPECT_TRUE(callbackResult);
}

TEST(FakeEmailSenderTest, CanSimulateFailure)
{
    FakeEmailSender sender;
    sender.setShouldSucceed(false);

    bool callbackResult = true;
    sender.sendEmail("bob@example.com", "subject", "body", [&](bool ok) { callbackResult = ok; });

    EXPECT_FALSE(callbackResult);
}

TEST(FakeEmailSenderTest, ClearRemovesSentMessages)
{
    FakeEmailSender sender;
    sender.sendEmail("a@example.com", "s", "b", [](bool) {});
    sender.clear();
    EXPECT_EQ(sender.count(), 0u);
}
