#pragma once

// Task 15 (authforge-sdk-refactor, design.md §6/§8): fake implementations
// of common::ports interfaces. See FakeClock.h for the placement
// rationale.
//
// FakeEmailSender: a capturing IEmailSender that records every send
// request instead of making a real SMTP connection, so a test can assert
// on exactly what would have been sent (recipient/subject/body) without
// any network dependency. Configurable to simulate send failure.

#include <authforge/common/ports/IEmailSender.h>

#include <string>
#include <vector>

namespace authforge::common::testing
{

class FakeEmailSender : public authforge::common::ports::IEmailSender
{
  public:
    struct SentMessage
    {
        std::string to;
        std::string subject;
        std::string body;
    };

    void sendEmail(
      const std::string &to,
      const std::string &subject,
      const std::string &body,
      SentCallback &&callback
    ) override
    {
        sent_.push_back(SentMessage{to, subject, body});
        if (callback)
            callback(shouldSucceed_);
    }

    /// All messages "sent" so far, in call order.
    const std::vector<SentMessage> &sent() const
    {
        return sent_;
    }

    /// Number of send attempts.
    size_t count() const
    {
        return sent_.size();
    }

    /// Configure whether future sendEmail() calls report success (true,
    /// the default) or failure (false) via their callback.
    void setShouldSucceed(bool shouldSucceed)
    {
        shouldSucceed_ = shouldSucceed;
    }

    /// Discard all captured sent messages.
    void clear()
    {
        sent_.clear();
    }

  private:
    std::vector<SentMessage> sent_;
    bool shouldSucceed_ = true;
};

}  // namespace authforge::common::testing
