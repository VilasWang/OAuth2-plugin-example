#pragma once

// Task 17 slice 3 (authforge-sdk-refactor, design.md §6/§7.2 评审 F4):
// ports oauth2::UserRef (OAuth2Plugin/include/oauth2/storage/UserRef.h)
// into authforge::oauth2::model, unchanged. See that header's own comment
// for the full rationale -- summarized here: IConsentRepository must not
// expose identity's internal primary key (`int32_t internalUserId`)
// directly in its signatures (couples oauth2's decision layer to
// identity's storage details). UserRef is the deliberately minimal,
// opaque-to-oauth2-decision-code carrier used until a real subject
// resolution flow (authforge::common::ports::ISubjectResolver, already
// shipped in Task 13) is wired in by the product layer (a later M3
// assembly task) to actually produce this value.
//
// Only the storage-layer implementations of IConsentRepository are
// allowed to unwrap `internalUserId` to build a query/key -- oauth2
// decision code must treat this as opaque.

#include <cstdint>

namespace authforge::oauth2::model
{

/**
 * @brief Transitional, opaque reference to a user for oauth2-side ports.
 * See this header's own comment above (and the original
 * OAuth2Plugin/include/oauth2/storage/UserRef.h) for the full rationale.
 */
struct UserRef
{
    /// Placeholder payload: today, callers only ever have identity's
    /// internal user id available. Do not add interpretation logic here
    /// beyond storing this value.
    int32_t internalUserId = 0;
};

}  // namespace authforge::oauth2::model
