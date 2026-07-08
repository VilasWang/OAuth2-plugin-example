#pragma once

// Task 13 (authforge-sdk-refactor, design.md §3.1/§5.1/§6): value objects
// for the shared Domain kernel. TokenValue wraps an opaque bearer token
// string (access token or refresh token value) so Domain method signatures
// can say TokenValue instead of a bare std::string, preventing accidental
// mixups with ClientId/Subject/etc at a call site (design.md §3.1: "消除裸
// std::string 引发的校验/注入类缺陷").
//
// No format grammar is imposed: the existing token generation (TokenService)
// produces opaque random strings, not a structured format this value object
// should parse or validate beyond non-empty.

#include <stdexcept>
#include <string>
#include <utility>

namespace authforge::common::model
{

/**
 * @brief Opaque, immutable bearer token value (access or refresh token).
 */
class TokenValue
{
  public:
    /// Construct from a raw token string. Throws std::invalid_argument if
    /// `value` is empty.
    explicit TokenValue(std::string value) : value_(std::move(value))
    {
        if (value_.empty())
        {
            throw std::invalid_argument("TokenValue: value must not be empty");
        }
    }

    const std::string &value() const noexcept
    {
        return value_;
    }

    bool operator==(const TokenValue &other) const noexcept
    {
        return value_ == other.value_;
    }

    bool operator!=(const TokenValue &other) const noexcept
    {
        return !(*this == other);
    }

  private:
    std::string value_;
};

}  // namespace authforge::common::model
