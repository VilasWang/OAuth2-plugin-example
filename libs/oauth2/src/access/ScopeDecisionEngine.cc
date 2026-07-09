#include <authforge/oauth2/access/ScopeDecisionEngine.h>

namespace authforge::oauth2::access
{

bool isAdminScope(const std::string &scope)
{
    // Reproduces IdentityService::scopeRequiresAdminRole
    // (OAuth2Plugin/src/services/IdentityService.cc) exactly: same
    // hardcoded list, same "exact match OR prefix followed by ':'"
    // semantics (e.g. "admin:read:extra" matches via the "admin:" prefix
    // rule, not just the literal "admin:read" entry).
    static const std::vector<std::string> adminScopes =
      {"admin", "admin:read", "admin:write", "user:manage", "settings:manage"};

    for (const auto &adminScope : adminScopes)
    {
        if (scope == adminScope || scope.find(adminScope + ":") == 0)
        {
            return true;
        }
    }
    return false;
}

ScopeCheckResult evaluateScope(
  const std::string &scope,
  const authforge::oauth2::model::Client &client,
  bool hasAdminRole,
  bool hasConsent
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

    if (isAdminScope(scope) && !hasAdminRole)
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
  const authforge::oauth2::model::Client &client,
  bool hasAdminRole,
  const std::function<bool(const std::string &)> &hasConsentForScope
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
          evaluateScope(scope, client, hasAdminRole, /*hasConsent=*/true).decision !=
          ScopeDecision::Invalid;

        const bool hasConsent = passesAllowlistAndRole && hasConsentForScope(scope);
        ScopeCheckResult result = evaluateScope(scope, client, hasAdminRole, hasConsent);

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

}  // namespace authforge::oauth2::access
