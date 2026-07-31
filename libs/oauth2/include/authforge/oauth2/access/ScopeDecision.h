#pragma once

// Task 17 slice 7 (authforge-sdk-refactor, design.md §6's directory
// layout: "access/ # consent + scope 分层策略 + 决策引擎"): the
// consent+scope decision engine's result types.
//
// Ports the THREE-STATE decision (VALID / INVALID / CONSENT_REQUIRED)
// that the pre-existing production flow currently expresses as an
// implicit sequence of separate calls rather than a single typed result:
//   - ClientService::validateClientScopes (Tier 1: client allowlist)
//   - IdentityService::validateUserRolesForScopes +
//     IdentityService::scopeRequiresAdminRole (Tier 2: admin role)
//   - OAuth2StandardController::checkUserConsentAndProceed's direct
//     plugin->hasUserConsent(...) calls (Tier 3: per-scope user consent,
//     driving the recursive scope-by-scope walk that decides whether to
//     redirect to /consent)
//
// This mirrors a design that was drafted but never implemented in this
// codebase (see docs/design/superpowers/specs/2026-05-06-oauth2-security-
// compliance-design-v5.1.md's ScopeValidationStatus/ScopeCheckResult/
// ScopeValidationSummary) -- ScopeDecisionEngine.h (this slice) is a
// fresh, Domain-layer (Drogon-free) implementation of that same shape,
// not a copy of that draft's code (which was itself never wired into
// production and targeted the old, undivided IOAuth2Storage).

#include <string>
#include <vector>

namespace authforge::oauth2::access
{

/// The outcome of evaluating one requested scope against a client's
/// allowlist, the user's roles (for admin-tiered scopes), and the user's
/// recorded consent.
enum class ScopeDecision
{
    /// Scope is allowed for this client, the user has any role required
    /// for it, and (if applicable) the user has already consented.
    Valid,
    /// Scope is rejected outright: not in the client's allowlist, or the
    /// user lacks a role required for an admin-tiered scope.
    Invalid,
    /// Scope would otherwise be Valid, but the user has not yet recorded
    /// consent for it -- the caller should route to a consent flow rather
    /// than treating this as a hard failure.
    ConsentRequired,
};

/// The result of evaluating a single scope.
struct ScopeCheckResult
{
    std::string scope;
    ScopeDecision decision = ScopeDecision::Invalid;
    /// Machine-readable reason code (e.g. "scope_not_allowed_for_client",
    /// "admin_role_required", "user_consent_required"). Empty when
    /// decision == Valid.
    std::string reason;
};

/// The aggregated result of evaluating a list of requested scopes.
struct ScopeValidationSummary
{
    std::vector<std::string> valid;
    std::vector<std::string> invalid;
    std::vector<std::string> consentRequired;
    /// Reason codes for each entry in `invalid`, in the same order.
    std::vector<std::string> invalidReasons;

    /// True iff every requested scope was Valid (no Invalid, no
    /// ConsentRequired entries) -- the caller can proceed to issue a
    /// grant immediately.
    bool canProceed() const noexcept
    {
        return invalid.empty() && consentRequired.empty();
    }

    /// True iff at least one requested scope needs user consent (and none
    /// were outright Invalid -- Invalid takes precedence as a hard
    /// failure the caller must handle before consent is even relevant).
    bool needsConsent() const noexcept
    {
        return invalid.empty() && !consentRequired.empty();
    }

    /// True iff at least one requested scope was rejected outright.
    bool hasErrors() const noexcept
    {
        return !invalid.empty();
    }
};

}  // namespace authforge::oauth2::access
