#pragma once

// Task 17 slice 7 (authforge-sdk-refactor, design.md §6's directory
// layout: "access/ # consent + scope 分层策略 + 决策引擎"): the
// consent+scope decision engine itself.
//
// Design choice: PURE, SYNCHRONOUS evaluation over already-resolved
// facts (a Client aggregate, whether the caller has an admin role,
// whether the caller has consented), rather than an engine that itself
// performs the async I/O (role lookup, consent lookup) the pre-existing
// production flow interleaves with its scope-by-scope walk
// (IdentityService::validateUserRolesForScopes,
// OAuth2StandardController::checkUserConsentAndProceed's
// plugin->hasUserConsent chain). Keeping the engine pure means:
//   - It is trivially unit-testable (no callbacks/fakes needed for the
//     engine itself -- only for whatever future orchestrator resolves the
//     facts it consumes).
//   - The actual async orchestration (fetch roles via IRoleProvider,
//     fetch consent via IConsentRepository, one scope at a time or in
//     parallel) is a decision for the eventual AuthorizationService
//     migration slice (not yet done -- see PROGRESS.md), which can choose
//     its own concurrency strategy without the engine's logic needing to
//     change.
//
// isAdminScope() reproduces IdentityService::scopeRequiresAdminRole's
// exact existing hardcoded admin-scope-prefix list and matching
// semantics (scope == adminScope || scope.find(adminScope + ":") == 0),
// so this engine's behavior on the current scope catalog is identical to
// production today.

#include <authforge/oauth2/access/ScopeDecision.h>
#include <authforge/oauth2/model/Client.h>

#include <functional>
#include <string>
#include <vector>

namespace authforge::oauth2::access
{

/**
 * @brief Evaluate a single requested scope against a client's allowlist,
 * whether the caller has an admin role (only consulted for admin-tiered
 * scopes), and whether the caller has already consented to this scope.
 *
 * Tier order (matches the existing production sequence):
 *  1. Client allowlist (Client::allowsScope) -- Invalid if not allowed.
 *  2. Admin-role requirement (scopeRequiresAdmin) -- Invalid if the scope
 *     is admin-tiered and `hasAdminRole` is false.
 *  3. Consent (`hasConsent`) -- ConsentRequired if the scope passed tiers
 *     1-2 but the caller has not yet consented.
 *
 * @param scope The scope being evaluated.
 * @param client The requesting client (for the allowlist check).
 * @param hasAdminRole Whether the caller has the "admin" role. Only
 * consulted if scopeRequiresAdmin(scope) is true; irrelevant otherwise.
 * @param hasConsent Whether the caller has already recorded consent for
 * this scope.
 * @param scopeRequiresAdmin #43 §5.5: predicate that returns true iff the
 * scope requires the admin role. Replaces the former hardcoded isAdminScope
 * -- the caller (AuthorizationService) supplies a predicate backed by the
 * DB oauth2_scopes.requires_admin_role column (loaded at startup), so the
 * admin-scope definition is data-driven and cannot drift from the catalog.
 */
ScopeCheckResult evaluateScope(
  const std::string &scope,
  const authforge::oauth2::model::Client &client,
  bool hasAdminRole,
  bool hasConsent,
  const std::function<bool(const std::string &)> &scopeRequiresAdmin
);

/**
 * @brief Evaluate a list of requested scopes, aggregating each
 * evaluateScope() result into a ScopeValidationSummary.
 *
 * @param scopes The requested scopes.
 * @param client The requesting client.
 * @param hasAdminRole Whether the caller has the "admin" role.
 * @param hasConsentForScope Callback invoked once per scope (in the order
 * given) to determine whether the caller has already consented to it.
 * Only invoked for scopes that pass tiers 1-2 (client allowlist + admin
 * role) -- consent is never checked for a scope that is already Invalid,
 * matching the existing production short-circuit behavior.
 * @param scopeRequiresAdmin #43 §5.5: predicate (see evaluateScope above).
 */
ScopeValidationSummary evaluateScopes(
  const std::vector<std::string> &scopes,
  const authforge::oauth2::model::Client &client,
  bool hasAdminRole,
  const std::function<bool(const std::string &)> &hasConsentForScope,
  const std::function<bool(const std::string &)> &scopeRequiresAdmin
);

}  // namespace authforge::oauth2::access
