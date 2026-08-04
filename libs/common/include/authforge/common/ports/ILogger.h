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
// Level set mirrors Drogon's LOG_* macro names 1:1 (Trace/Debug/Info/Warn/
// Error/Fatal), so a call-site migration is a mechanical macro-to-method-call
// rename, not a redesign. Trace was originally omitted on the assumption that
// no LOG_TRACE existed in the migration scope; that assumption no longer holds
// -- the generated ORM model headers emit LOG_TRACE for SQL strings, and Trace
// is now the documented tier for the finest-grained tracing (function args/
// return values, SQL, per-iteration detail) per the repo logging standard
// (docs/backend/observability.md §3.2). Order matches Drogon/Trantor's own
// severity ordering: Trace < Debug < Info < Warn < Error < Fatal.

#include <string>

namespace authforge::common::ports
{

enum class LogLevel
{
    Trace,
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
