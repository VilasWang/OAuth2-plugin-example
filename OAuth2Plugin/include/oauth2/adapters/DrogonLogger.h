#pragma once

// Task 14 (authforge-sdk-refactor, design.md §5.6): Adapter-side default
// implementation of authforge::common::ports::ILogger, backed by Drogon's
// own logger (design.md port table: "Drogon 日志适配"). This is the one
// ILogger implementation that DOES depend on Drogon -- that is expected
// and correct: it lives in OAuth2Plugin/include/oauth2/adapters/ (Adapter
// layer, design.md §4.1 rule 3: "Adapter 层...允许依赖 Drogon"), not in
// libs/common (Domain layer, which must not).
//
// Placement note: same rationale as OpenSslCryptoProvider.h -- kept under
// OAuth2Plugin/include/oauth2/adapters/ rather than a not-yet-created
// libs/drogon (M3, Task 20); a later milestone's directory move (Task 39)
// is expected to relocate this file verbatim.

#include <authforge/common/ports/ILogger.h>

namespace authforge::drogon::adapters
{

/**
 * @brief Forwards ILogger calls to Drogon's LOG_* macros (trantor logger).
 * Stateless; safe to use a single shared instance from any thread (Drogon's
 * own logger is itself safe for concurrent use, being the same sink every
 * LOG_* call site in this codebase already funnels through).
 */
class DrogonLogger : public authforge::common::ports::ILogger
{
  public:
    void log(authforge::common::ports::LogLevel level, const std::string &message) override;
};

}  // namespace authforge::drogon::adapters
