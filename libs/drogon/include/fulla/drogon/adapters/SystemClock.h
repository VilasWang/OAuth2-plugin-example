#pragma once

// Task 14 (fulla-sdk-refactor, design.md §5.6): Adapter-side default
// implementation of fulla::common::ports::IClock, backed by
// std::chrono::system_clock (design.md port table: "system_clock 实现").

#include <fulla/common/ports/IClock.h>

namespace fulla::drogon::adapters
{

class SystemClock : public fulla::common::ports::IClock
{
  public:
    int64_t nowSeconds() const override;
    int64_t nowMilliseconds() const override;
};

}  // namespace fulla::drogon::adapters
