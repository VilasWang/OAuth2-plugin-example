#pragma once

// M2b Task 17 slice 5 (authforge-sdk-refactor, design.md §6/§8): fake
// implementation of authforge::common::ports::IAuditSink. See
// FakeLogger.h for the placement/pattern rationale (this class mirrors it
// exactly): a capturing sink that records every emitted AuditEvent
// instead of persisting it anywhere, so a test can assert on exactly
// which events Domain code emitted.

#include <authforge/common/ports/IAuditSink.h>

#include <vector>

namespace authforge::common::testing
{

class FakeAuditSink : public authforge::common::ports::IAuditSink
{
  public:
    void record(const authforge::common::observability::AuditEvent &event) override
    {
        events_.push_back(event);
    }

    /// All captured events, in call order.
    const std::vector<authforge::common::observability::AuditEvent> &events() const
    {
        return events_;
    }

    /// True iff any captured event's `action` field equals `action`.
    bool hasEventWithAction(const std::string &action) const
    {
        for (const auto &event : events_)
        {
            if (event.action == action)
                return true;
        }
        return false;
    }

    /// Number of captured events.
    size_t count() const
    {
        return events_.size();
    }

    /// Discard all captured events.
    void clear()
    {
        events_.clear();
    }

  private:
    std::vector<authforge::common::observability::AuditEvent> events_;
};

}  // namespace authforge::common::testing
