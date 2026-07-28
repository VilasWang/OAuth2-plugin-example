#include <authforge/drogon/adapters/SystemClock.h>

#include <chrono>

namespace authforge::drogon::adapters
{

int64_t SystemClock::nowSeconds() const
{
    return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch()
    )
      .count();
}

int64_t SystemClock::nowMilliseconds() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch()
    )
      .count();
}

}  // namespace authforge::drogon::adapters
