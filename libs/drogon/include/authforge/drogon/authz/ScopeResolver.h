#pragma once

// Scope implication resolver for the resource-scope authorization model
// (#43). Pure, framework-free functions that decide whether a token's scope
// set satisfies a ResourceScopeRequirement, with per-requirement implication.
//
// Semantics (RFC 6749 §3.3 scope tokens are space-delimited; RFC 6750 §3.1
// insufficient_scope is the rejection when the gate fails):
//
//   A token scope S satisfies a required scope R when:
//     exact:  S == R, OR
//     implied: R appears in this requirement's `impliedBy` AND S is one of
//              the token's scopes (e.g. token "admin" satisfies a
//              requirement {scopes:["users:read"], impliedBy:["admin"]}).
//
//   The OIDC standard scopes (openid/profile/email) are NEVER implied by
//   `admin` because their requirements carry an empty `impliedBy` -- a
//   userinfo request always needs an actual user token carrying `openid`.
//
// Implication is per-requirement (declared on each route's EndpointInfo),
// not a global scope graph, so there is no hardcoded implication list to
// drift. Building a transitive implication closure is a non-goal (the model
// is a shallow admin->leaves fan-out, declared explicitly per route).

#include <authforge/drogon/authz/ResourceScopeRegistry.h>
#include <authforge/drogon/utils/ScopeChecker.h>

#include <sstream>
#include <string>
#include <string_view>

namespace authforge::drogon::authz
{

/// True iff a single required scope `required` is satisfied by the token's
/// space-delimited scope string: either the token carries `required` exactly,
/// or it carries one of `impliedBy`.
inline bool scopeSatisfied(
  std::string_view tokenScopes,
  const std::string &required,
  const std::vector<std::string> &impliedBy)
{
    if (utils::hasScope(tokenScopes, required))
        return true;
    for (const auto &imp : impliedBy)
    {
        if (utils::hasScope(tokenScopes, imp))
            return true;
    }
    return false;
}

/// True iff the token's scope string satisfies the whole requirement.
/// ScopeMatch::All = every required scope satisfied; Any = at least one.
/// An empty requirement.scopes is vacuously satisfied (no requirement).
inline bool satisfies(std::string_view tokenScopes, const ResourceScopeRequirement &req)
{
    if (req.scopes.empty())
        return true;
    if (req.match == ScopeMatch::All)
    {
        for (const auto &s : req.scopes)
        {
            if (!scopeSatisfied(tokenScopes, s, req.impliedBy))
                return false;
        }
        return true;
    }
    // ScopeMatch::Any
    for (const auto &s : req.scopes)
    {
        if (scopeSatisfied(tokenScopes, s, req.impliedBy))
            return true;
    }
    return false;
}

/// Build the RFC 6750 §3 WWW-Authenticate challenge value (the part after
/// "Bearer ") for an insufficient_scope rejection. The `scope` attribute is
/// a SPACE-DELIMITED list of the scopes that would unlock the resource
/// (RFC 6750 §3: "scope" auth-param is space-delimited) -- R1: multi-scope
/// requirements join with spaces, not a single token.
inline std::string buildInsufficientScopeChallenge(const ResourceScopeRequirement &req)
{
    std::ostringstream scopeAttr;
    for (size_t i = 0; i < req.scopes.size(); ++i)
    {
        if (i > 0)
            scopeAttr << " ";
        scopeAttr << req.scopes[i];
    }
    return "Bearer realm=\"authforge\", error=\"insufficient_scope\", "
           "error_description=\"The access token does not have the required scope\", "
           "scope=\"" +
           scopeAttr.str() + "\"";
}

}  // namespace authforge::drogon::authz
