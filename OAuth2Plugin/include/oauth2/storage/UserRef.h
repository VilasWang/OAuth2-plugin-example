#pragma once

#include <cstdint>

namespace oauth2
{

/**
 * @brief Transitional, opaque reference to a user for oauth2-side ports.
 *
 * Design decision (evaluation F4, design.md §7.2 / §5.2):
 * `IConsentRepository` must not expose the identity subsystem's internal
 * primary key (`int32_t internalUserId`) directly in its method signatures,
 * because that couples the oauth2 decision layer to identity's storage
 * details. In the target architecture (M2a `libs/common`), a real
 * `authforge::common::ports::ISubjectResolver` will resolve an opaque
 * `Subject` (`provider:localId`) into whatever key identity's repositories
 * need, and hand back a proper abstraction to `oauth2`.
 *
 * That port does not exist yet (it ships in M2a, Task 13). `UserRef` is a
 * deliberately minimal placeholder that lets Task 7 define
 * `IConsentRepository` without a bare `int32_t internalUserId` parameter,
 * while remaining trivially convertible from the current callers (which
 * only have an `int32_t internalUserId` on hand today).
 *
 * Rules while this placeholder is in effect:
 * - oauth2-side decision code MUST treat `UserRef` as opaque: construct it
 *   from whatever identifier is available, pass it through
 *   `IConsentRepository`, and never branch on `internalUserId`'s value.
 * - Only the storage-layer implementations of `IConsentRepository` (Task 9
 *   `PostgresOAuth2Storage` split, Task 10 Redis/Memory split) are allowed
 *   to unwrap `internalUserId` to build a query/key, exactly as the current
 *   `IOAuth2Storage::hasUserConsent(int32_t internalUserId, ...)` etc. do.
 * - When M2a lands `ISubjectResolver`, this type is expected to be replaced
 *   by whatever `common` value object that port returns (or `UserRef`
 *   itself moves to `common` and gains a proper opaque token instead of a
 *   raw int). That migration is out of scope for Task 7.
 *
 * File placement: kept in its own header (rather than nested inside
 * `IConsentRepository.h`) because it is a value type, not part of the
 * repository contract's shape, and because a future `libs/common` move
 * (M2a) only has to relocate one small file instead of extracting a type
 * out of an interface header.
 */
struct UserRef
{
    /**
     * @brief Placeholder payload. Callers today only ever have the
     * identity subsystem's internal user id available, so that's what is
     * carried. Do not add interpretation logic here beyond storing this
     * value: this is a data-carrier, not a place to grow identity-aware
     * behavior in the oauth2 decision layer.
     */
    int32_t internalUserId = 0;
};

}  // namespace oauth2
