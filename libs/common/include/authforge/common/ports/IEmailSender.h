#pragma once

// Task 13 (authforge-sdk-refactor, design.md §5.6/§6): libs/common ports.
//
// IEmailSender is the Domain-facing port for outbound email (password
// reset/verification links, MFA notices, etc), per design.md §5.6's port
// table: "IEmailSender | libcurl SMTP | libcurl 实现". Named IEmailSender
// (not IEmailService) here to avoid a naming collision with the existing
// oauth2::IEmailService (OAuth2Plugin/include/oauth2/utils/EmailService.h)
// during the M2a transition period where both may exist side by side; the
// method shape is intentionally identical to that existing interface
// (sendEmail(to, subject, body, callback)) so migrating a call site is a
// type swap, not a redesign. The existing SmtpEmailService/
// ConsoleEmailService implementations already have no Drogon dependency
// (email is sent via libcurl, not Drogon) -- Task 14/16 is expected to
// either move them under this port directly or adapt them, not rewrite
// their SMTP logic.

#include <functional>
#include <string>

namespace authforge::common::ports
{

/**
 * @brief Sends outbound email. The default production implementation is
 * Adapter-side (libcurl SMTP, design.md §5.6); a test double can capture
 * sent messages without any network dependency.
 */
class IEmailSender
{
  public:
    using SentCallback = std::function<void(bool)>;

    virtual ~IEmailSender() = default;

    /// Send an email. Invokes `callback` with true on success, false on
    /// failure (SMTP error, connection failure, etc).
    virtual void sendEmail(
      const std::string &to,
      const std::string &subject,
      const std::string &body,
      SentCallback &&callback
    ) = 0;
};

}  // namespace authforge::common::ports
