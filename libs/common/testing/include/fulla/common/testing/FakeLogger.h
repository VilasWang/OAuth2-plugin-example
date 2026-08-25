#pragma once

// Task 15 (fulla-sdk-refactor, design.md §6/§8): fake implementations
// of common::ports interfaces. See FakeClock.h for the placement
// rationale.
//
// FakeLogger: a capturing ILogger that records every log() call instead
// of emitting it anywhere, so a test can assert on exactly which messages
// (and at which level) Domain code logged -- without any Drogon
// dependency (design.md's whole point for this port).

#include <fulla/common/ports/ILogger.h>

#include <string>
#include <vector>

namespace fulla::common::testing
{

class FakeLogger : public fulla::common::ports::ILogger
{
  public:
    struct Entry
    {
        fulla::common::ports::LogLevel level;
        std::string message;
    };

    void log(fulla::common::ports::LogLevel level, const std::string &message) override
    {
        entries_.push_back(Entry{level, message});
    }

    /// All captured log entries, in call order.
    const std::vector<Entry> &entries() const
    {
        return entries_;
    }

    /// True iff any captured entry's message contains `substring`
    /// (optionally restricted to a specific level).
    bool hasMessageContaining(const std::string &substring) const
    {
        for (const auto &entry : entries_)
        {
            if (entry.message.find(substring) != std::string::npos)
                return true;
        }
        return false;
    }

    bool hasMessageContaining(
      fulla::common::ports::LogLevel level,
      const std::string &substring
    ) const
    {
        for (const auto &entry : entries_)
        {
            if (entry.level == level && entry.message.find(substring) != std::string::npos)
                return true;
        }
        return false;
    }

    /// Number of captured log entries.
    size_t count() const
    {
        return entries_.size();
    }

    /// Discard all captured entries.
    void clear()
    {
        entries_.clear();
    }

  private:
    std::vector<Entry> entries_;
};

}  // namespace fulla::common::testing
