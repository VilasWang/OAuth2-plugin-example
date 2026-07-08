#pragma once

// Task 13 (authforge-sdk-refactor, design.md §3.1/§5.1/§6): libs/common
// Domain kernel. Result<T,E> is a minimal, dependency-free (header-only,
// std-only) success/error union used by Domain-layer (oauth2/identity)
// service methods that need a synchronous return-or-error shape (as
// distinct from the existing async VoidCallback/XxxCallback repository
// methods, which stay callback-based per design.md's "Implementations use
// ASYNCHRONOUS CALLBACKS" convention -- Result<T,E> is for the *synchronous*
// parts of Domain logic, e.g. PKCE verification, value-object validation,
// or a service method that can fail before ever reaching an async
// repository call).
//
// Design notes:
//  - Deliberately minimal: no monadic map/and_then/or_else combinators.
//    Nothing in design.md or the M2a task list calls for a full functional
//    Result library; keeping this to construction + inspection + value
//    access avoids speculative API surface (see steering guidance: match
//    scope to what was asked).
//  - E defaults to authforge::common::error::Error (the Domain-wide error
//    type also used for the HTTP Error Envelope), but the template is not
//    hard-coded to it -- a Result<T, MyLocalEnum> is equally valid for a
//    narrower internal use.
//  - No exceptions are thrown by Result itself except value()/error() being
//    called on the wrong variant, which is a programming error (contract
//    violation), not a recoverable condition -- consistent with how e.g.
//    std::optional::value() throws std::bad_optional_access for the same
//    kind of misuse.

#include <authforge/common/error/ErrorTypes.h>

#include <stdexcept>
#include <utility>
#include <variant>

namespace authforge::common::result
{

/**
 * @brief Thrown by Result<T,E>::value() or Result<T,E>::error() when called
 * on a Result that does not hold that variant. Indicates a programming
 * error at the call site (should have checked ok()/isError() first).
 */
class BadResultAccess : public std::logic_error
{
  public:
    explicit BadResultAccess(const char *what) : std::logic_error(what)
    {
    }
};

/**
 * @brief A minimal success-or-error union, analogous to Rust's Result<T,E>
 * or C++23's std::expected<T,E> (not assumed available -- this project
 * targets C++17, see design.md §1.3).
 *
 * @tparam T Success value type.
 * @tparam E Error value type (defaults to authforge::common::error::Error).
 */
template <typename T, typename E = authforge::common::error::Error>
class Result
{
  public:
    /// Construct a success Result holding `value`.
    static Result ok(T value)
    {
        return Result(std::in_place_index<0>, std::move(value));
    }

    /// Construct an error Result holding `error`.
    static Result err(E error)
    {
        return Result(std::in_place_index<1>, std::move(error));
    }

    /// True iff this Result holds a success value.
    bool ok() const noexcept
    {
        return storage_.index() == 0;
    }

    /// True iff this Result holds an error value. Equivalent to !ok().
    bool isError() const noexcept
    {
        return storage_.index() == 1;
    }

    /// Explicit bool conversion: true iff ok() (mirrors std::optional /
    /// std::expected convention for "did this succeed").
    explicit operator bool() const noexcept
    {
        return ok();
    }

    /// Access the success value. Throws BadResultAccess if isError().
    const T &value() const &
    {
        if (!ok())
        {
            throw BadResultAccess("Result::value() called on an error Result");
        }
        return std::get<0>(storage_);
    }

    /// @overload
    T &value() &
    {
        if (!ok())
        {
            throw BadResultAccess("Result::value() called on an error Result");
        }
        return std::get<0>(storage_);
    }

    /// @overload (move out of an rvalue Result)
    T &&value() &&
    {
        if (!ok())
        {
            throw BadResultAccess("Result::value() called on an error Result");
        }
        return std::get<0>(std::move(storage_));
    }

    /// Access the error value. Throws BadResultAccess if ok().
    const E &error() const &
    {
        if (!isError())
        {
            throw BadResultAccess("Result::error() called on a success Result");
        }
        return std::get<1>(storage_);
    }

    /// @overload
    E &error() &
    {
        if (!isError())
        {
            throw BadResultAccess("Result::error() called on a success Result");
        }
        return std::get<1>(storage_);
    }

    /// @overload (move out of an rvalue Result)
    E &&error() &&
    {
        if (!isError())
        {
            throw BadResultAccess("Result::error() called on a success Result");
        }
        return std::get<1>(std::move(storage_));
    }

    /// Return value() if ok(), otherwise `fallback`. Never throws.
    T valueOr(T fallback) const &
    {
        return ok() ? std::get<0>(storage_) : std::move(fallback);
    }

    /// @overload (move out of an rvalue Result)
    T valueOr(T fallback) &&
    {
        return ok() ? std::get<0>(std::move(storage_)) : std::move(fallback);
    }

  private:
    template <typename... Args>
    explicit Result(std::in_place_index_t<0> tag, Args &&...args) :
      storage_(tag, std::forward<Args>(args)...)
    {
    }

    template <typename... Args>
    explicit Result(std::in_place_index_t<1> tag, Args &&...args) :
      storage_(tag, std::forward<Args>(args)...)
    {
    }

    std::variant<T, E> storage_;
};

}  // namespace authforge::common::result
