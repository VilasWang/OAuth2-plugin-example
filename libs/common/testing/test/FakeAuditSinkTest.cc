// M2b Task 17 slice 5 (fulla-sdk-refactor, design.md §6/§8): pure
// gtest unit tests for FakeAuditSink.

#include <fulla/common/testing/FakeAuditSink.h>

#include <gtest/gtest.h>

using namespace fulla::common::testing;
using fulla::common::observability::AuditEvent;

namespace
{

AuditEvent makeEvent(const std::string &action, const std::string &outcome = "success")
{
    AuditEvent event;
    event.action = action;
    event.outcome = outcome;
    event.actorType = "user";
    event.actorId = "alice";
    return event;
}

}  // namespace

TEST(FakeAuditSinkTest, CapturesEventsInOrder)
{
    FakeAuditSink sink;
    sink.record(makeEvent("token_issued"));
    sink.record(makeEvent("token_refreshed"));

    ASSERT_EQ(sink.count(), 2u);
    EXPECT_EQ(sink.events()[0].action, "token_issued");
    EXPECT_EQ(sink.events()[1].action, "token_refreshed");
}

TEST(FakeAuditSinkTest, HasEventWithActionFindsMatchingAction)
{
    FakeAuditSink sink;
    sink.record(makeEvent("token_issued"));

    EXPECT_TRUE(sink.hasEventWithAction("token_issued"));
    EXPECT_FALSE(sink.hasEventWithAction("nonexistent_action"));
}

TEST(FakeAuditSinkTest, ClearRemovesAllEvents)
{
    FakeAuditSink sink;
    sink.record(makeEvent("login_success"));
    sink.clear();
    EXPECT_EQ(sink.count(), 0u);
}

TEST(FakeAuditSinkTest, ThroughBasePointer_DispatchesCorrectly)
{
    FakeAuditSink sink;
    fulla::common::ports::IAuditSink &port = sink;
    port.record(makeEvent("client_authenticated"));

    EXPECT_TRUE(sink.hasEventWithAction("client_authenticated"));
}
