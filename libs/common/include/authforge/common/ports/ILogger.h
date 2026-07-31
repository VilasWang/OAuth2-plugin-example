#pragma once

// Task 13 (authforge-sdk-refactor, design.md §5.6/§6): libs/common ports.
//
// ILogger replaces Domain-layer use of Drogon's LOG_DEBUG/LOG_INFO/
// LOG_WARN/LOG_ERROR macros, per design.md §5.6's port table: "ILogger |
// LOG_DEBUG/INFO/WARN/ERROR 宏 | Drogon 日志适配 / 标准实现". design.md
// §5.6 notes the scale of this specific call site ("此外 Domain 大量使用
// Drogon 的 LOG_* 宏"; §14's audit puts it at "Domain 内 407 处 LOG_*"),
// which is exactly why Task 14 treats it as its own bounded migration
// slice ("每类端口一个 PR、单 PR 调用点数设上限") rather than folding it into
// ICryptoProvider's migration. This task only declares the port shape.
//
// Level set mirrors Drogon's LOG_* macro names 1:1 (TRACE is omitted --
// grep across the Domain call sites design.md's audit covers shows no
// LOG_TRACE usage in the migration scope) so a call-site migration is a
// mechanical macro-to-method-call rename, not a redesign.

#include <string>

namespace authforge::common::ports
{

enum class LogLevel
{
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

/**
 * @brief Structured logging sink for the Domain layer, decoupled from
 * Drogon's LOG_* macros (design.md §5.6). The default production
 * implementation is Adapter-side and typically forwards to Drogon's own
 * logger (design.md: "Drogon 日志适配"), but a test double can capture/
 * assert on log output without any Drogon dependency.
 */
class ILogger
{
  public:
    virtual ~ILogger() = default;

    /// Emit a single log line at the given level.
    virtual void log(LogLevel level, const std::string &message) = 0;
};

}  // namespace authforge::common::ports
