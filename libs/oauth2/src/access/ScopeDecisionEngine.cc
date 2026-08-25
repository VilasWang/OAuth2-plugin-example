#include <fulla/oauth2/access/ScopeDecisionEngine.h>

namespace fulla::oauth2::access
{

// #43 §5.5: the hardcoded isAdminScope() function is REMOVED. The Tier-2
// admin-role check is now driven by the `scopeRequiresAdmin` predicate
// supplied by the caller (AuthorizationService), which is backed by the DB
// oauth2_scopes.requires_admin_role column loaded at startup. This removes
// the drift risk: a new admin scope added to the catalog is automatically
// enforced without a code change here.

ScopeCheckResult evaluateScope(
  const std::string &scope,
  const fulla::oauth2::model::Client &client,
  bool hasAdminRole,
  bool hasConsent,
  const std::function<bool(const std::string &)> &scopeRequiresAdmin
)
{
    ScopeCheckResult result;
    result.scope = scope;

    if (!client.allowsScope(scope))
    {
        result.decision = ScopeDecision::Invalid;
        result.reason = "scope_not_allowed_for_client";
        return result;
    }

    if (scopeRequiresAdmin(scope) && !hasAdminRole)
    {
        result.decision = ScopeDecision::Invalid;
        result.reason = "admin_role_required";
        return result;
    }

    if (!hasConsent)
    {
        result.decision = ScopeDecision::ConsentRequired;
        result.reason = "user_consent_required";
        return result;
    }

    result.decision = ScopeDecision::Valid;
    return result;
}

ScopeValidationSummary evaluateScopes(
  const std::vector<std::string> &scopes,
  const fulla::oauth2::model::Client &client,
  bool hasAdminRole,
  const std::function<bool(const std::string &)> &hasConsentForScope,
  const std::function<bool(const std::string &)> &scopeRequiresAdmin
)
{
    ScopeValidationSummary summary;

    for (const auto &scope : scopes)
    {
        // Short-circuit consent lookup for scopes that are already
        // Invalid on tiers 1-2 (client allowlist / admin role), matching
        // production's existing behavior of never even asking about
        // consent for a scope the client/role checks already rejected.
        //
        // evaluateScope() itself always takes a `hasConsent` bool, so
        // determine tier-1/2 validity first via a throwaway call with
        // hasConsent=true (i.e. "if consent were granted, would this
        // still be Invalid?") to decide whether hasConsentForScope needs
        // to be invoked at all.
        const bool passesAllowlistAndRole =
          evaluateScope(scope, client, hasAdminRole, /*hasConsent=*/true, scopeRequiresAdmin)
            .decision != ScopeDecision::Invalid;

        const bool hasConsent = passesAllowlistAndRole && hasConsentForScope(scope);
        ScopeCheckResult result =
          evaluateScope(scope, client, hasAdminRole, hasConsent, scopeRequiresAdmin);

        switch (result.decision)
        {
            case ScopeDecision::Valid:
                summary.valid.push_back(scope);
                break;
            case ScopeDecision::Invalid:
                summary.invalid.push_back(scope);
                summary.invalidReasons.push_back(result.reason);
                break;
            case ScopeDecision::ConsentRequired:
                summary.consentRequired.push_back(scope);
                break;
        }
    }

    return summary;
}

}  // namespace fulla::oauth2::access
