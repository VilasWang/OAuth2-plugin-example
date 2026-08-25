#pragma once

// Task 13 (fulla-sdk-refactor, design.md §3.1/§5.1/§6): value objects
// for the shared Domain kernel. See design.md §3.1: "值对象（Value
// Object）：Scope / Subject（如 local:alice）/ TokenValue / ClientId /
// RedirectUri / PkceChallenge / TenantId。消除裸 std::string 引发的校验/注入
// 类缺陷。"
//
// Subject is the OIDC-style "provider:localId" identity reference (e.g.
// "local:alice", "google:109283746..."). It is intentionally a thin,
// non-validating wrapper at this stage: design.md does not specify a
// canonical provider registry or format grammar beyond the "provider:id"
// shape already used informally by the existing subject-mapping code
// (ISubjectMappingRepository / getInternalUserId(subject, provider, ...)).
// Construction from a raw string is therefore accepted as-is; the one
// invariant enforced here is "non-empty", which is the only property every
// existing caller already relies on (an empty subject can never resolve to
// a real user).

#include <stdexcept>
#include <string>
#include <utility>

namespace fulla::common::model
{

/**
 * @brief Opaque, immutable reference to an identity subject (e.g.
 * "local:alice", "google:10928374"). Passed across the oauth2/identity
 * port boundary (design.md §5.2) instead of a bare std::string so the
 * boundary's type is self-documenting and cannot be confused with an
 * unrelated string field (ClientId, TokenValue, etc).
 */
class Subject
{
  public:
    /// Construct from a raw "provider:localId"-shaped string. Throws
    /// std::invalid_argument if `value` is empty.
    explicit Subject(std::string value) : value_(std::move(value))
    {
        if (value_.empty())
        {
            throw std::invalid_argument("Subject: value must not be empty");
        }
    }

    const std::string &value() const noexcept
    {
        return value_;
    }

    bool operator==(const Subject &other) const noexcept
    {
        return value_ == other.value_;
    }

    bool operator!=(const Subject &other) const noexcept
    {
        return !(*this == other);
    }

  private:
    std::string value_;
};

}  // namespace fulla::common::model
