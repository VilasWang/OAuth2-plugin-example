#pragma once

// Task 13 (authforge-sdk-refactor, design.md §5.6/§6): libs/common ports.
// IClock replaces Domain-layer direct calls to std::chrono::system_clock::now()
// (used e.g. for TOTP time-step computation and token expiry comparisons)
// with an injectable seam, per design.md §5.6's port table: "IClock |
// now（TOTP/过期判断） | system_clock 实现". The primary motivation is
// testability (a fake clock lets Domain unit tests exercise TOTP/expiry
// logic deterministically -- design.md §5.6/M2a Task 15: "Domain 可用假时钟/
// 假 crypto 做确定性单测"), not a Drogon dependency removal (std::chrono has
// never depended on Drogon) -- but the port still belongs here because the
// call sites it replaces are Domain code that should not reach for a
// global/static clock function directly.

#include <cstdint>

namespace authforge::common::ports
{

/**
 * @brief Provides the current time, as an injectable seam for
 * deterministic Domain-layer tests (fake clock) vs. production (real
 * system clock).
 */
class IClock
{
  public:
    virtual ~IClock() = default;

    /// Current time as Unix epoch seconds.
    virtual int64_t nowSeconds() const = 0;

    /// Current time as Unix epoch milliseconds.
    virtual int64_t nowMilliseconds() const = 0;
};

}  // namespace authforge::common::ports
