#pragma once

// Task 13 (fulla-sdk-refactor, design.md §3.1/§5.1/§6): value objects
// for the shared Domain kernel.
//
// Scope represents a single RFC 6749 §3.3 "scope-token"
// (scope-token = 1*NQCHAR, i.e. one space/backslash/dquote-free token; the
// space-delimited "scope" *parameter* on the wire is a sequence of these
// tokens, not a single Scope value). Keeping Scope as one token (rather
// than the whole delimited string) matches how existing scope-policy code
// already reasons about scopes one-at-a-time (e.g. "scope X 需 admin" in
// design.md §5.3's scope-tiering description) and lets
// oauth2::access::ScopePolicy (a later M2b task) work with a
// std::vector<Scope> instead of re-splitting a raw string at every call
// site.

#include <stdexcept>
#include <string>
#include <utility>

namespace fulla::common::model
{

/**
 * @brief A single OAuth2 scope token (RFC 6749 §3.3 scope-token grammar:
 * NQCHAR = %x21 / %x23-5B / %x5D-7E, i.e. printable ASCII excluding space
 * (%x20), dquote (%x22) and backslash (%x5C)).
 */
class Scope
{
  public:
    /// Construct from a single scope-token string. Throws
    /// std::invalid_argument if `value` is empty or contains a character
    /// outside the RFC 6749 NQCHAR grammar (space, dquote, or backslash).
    explicit Scope(std::string value) : value_(std::move(value))
    {
        if (value_.empty())
        {
            throw std::invalid_argument("Scope: value must not be empty");
        }
        for (unsigned char c : value_)
        {
            if (c == ' ' || c == '"' || c == '\\' || c < 0x21 || c > 0x7E)
            {
                throw std::invalid_argument(
                  "Scope: value must be a single RFC 6749 scope-token "
                  "(no whitespace/quote/backslash): '" +
                  value_ + "'"
                );
            }
        }
    }

    const std::string &value() const noexcept
    {
        return value_;
    }

    bool operator==(const Scope &other) const noexcept
    {
        return value_ == other.value_;
    }

    bool operator!=(const Scope &other) const noexcept
    {
        return !(*this == other);
    }

    bool operator<(const Scope &other) const noexcept
    {
        return value_ < other.value_;
    }

  private:
    std::string value_;
};

}  // namespace fulla::common::model
