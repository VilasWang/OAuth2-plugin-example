#pragma once

// Task 14 (authforge-sdk-refactor, design.md §5.6): Adapter-side default
// implementation of authforge::common::ports::IClock, backed by
// std::chrono::system_clock (design.md port table: "system_clock 实现").

#include <authforge/common/ports/IClock.h>

namespace authforge::drogon::adapters
{

class SystemClock : public authforge::common::ports::IClock
{
  public:
    int64_t nowSeconds() const override;
    int64_t nowMilliseconds() const override;
};

}  // namespace authforge::drogon::adapters
