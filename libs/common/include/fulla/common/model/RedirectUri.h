#pragma once

// Task 13 (fulla-sdk-refactor, design.md §3.1/§5.1/§6): value objects
// for the shared Domain kernel. RedirectUri wraps the OAuth2 redirect_uri
// string (design.md §3.1). Validation is intentionally minimal (non-empty
// only): RFC 6749 §3.1.2 redirect_uri validation (absolute URI, no
// fragment, exact registered-URI match) is a *comparison against the
// client's registered URI list*, which requires repository access this
// value object does not have -- that check stays in
// IGrantRepository::consumeAuthCode (design.md §7.2 "consumeAuthCode 必须
// 校验 redirect_uri 匹配", already implemented and preserved verbatim by
// Task 7-9). This type exists so the oauth2 Domain's method signatures can
// say RedirectUri instead of a bare std::string at the port/service
// boundary, not to duplicate that repository-level check.

#include <stdexcept>
#include <string>
#include <utility>

namespace fulla::common::model
{

/**
 * @brief Opaque, immutable OAuth2 redirect_uri value.
 */
class RedirectUri
{
  public:
    /// Construct from a raw redirect_uri string. Throws
    /// std::invalid_argument if `value` is empty.
    explicit RedirectUri(std::string value) : value_(std::move(value))
    {
        if (value_.empty())
        {
            throw std::invalid_argument("RedirectUri: value must not be empty");
        }
    }

    const std::string &value() const noexcept
    {
        return value_;
    }

    bool operator==(const RedirectUri &other) const noexcept
    {
        return value_ == other.value_;
    }

    bool operator!=(const RedirectUri &other) const noexcept
    {
        return !(*this == other);
    }

  private:
    std::string value_;
};

}  // namespace fulla::common::model
