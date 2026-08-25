#pragma once

// Single insufficient_scope emitter for the resource-scope authorization
// model (#43). RFC 6750 §3.1: when an access token has insufficient scope,
// the resource server responds HTTP 403 with a WWW-Authenticate challenge
// carrying error="insufficient_scope" AND a `scope` auth-param naming the
// scope(s) that would unlock the resource.
//
// Consolidates the three former ad-hoc emitters (OAuth2AuthFilter,
// AuthorizationFilter, and TokenEndpointController::userInfo) so the
// challenge shape (including the previously-omitted `scope` attribute on the
// userinfo path) is uniform everywhere. R1: the `scope` attribute is a
// space-delimited list when a requirement names multiple scopes.

#include <fulla/drogon/authz/ScopeResolver.h>
#include <fulla/common/error/ErrorTypes.h>
#include <fulla/drogon/error/ErrorResponder.h>
#include <fulla/drogon/error/RequestId.h>
#include <drogon/drogon.h>

#include <string>

namespace fulla::drogon::authz
{

/// Build the RFC 6750 §3.1 insufficient_scope 403 response: the standard
/// error envelope (AUTHZ_INSUFFICIENT_PERMISSIONS -> 403) plus a
/// WWW-Authenticate challenge whose `scope` attribute names the required
/// scope(s). Callers pass the result to their FilterCallback (fcb).
inline ::drogon::HttpResponsePtr respondInsufficientScope(
  const ::drogon::HttpRequestPtr &req,
  const ResourceScopeRequirement &reqmt)
{
    auto error = fulla::common::error::Error::fromCode(
      "AUTHZ_INSUFFICIENT_PERMISSIONS", fulla::common::error::RequestId::resolve(req)
    );
    error.message = "Insufficient scope for this resource";
    auto resp = fulla::common::error::ErrorResponder::buildResponse(req, error);
    resp->addHeader("WWW-Authenticate", buildInsufficientScopeChallenge(reqmt));
    return resp;
}

}  // namespace fulla::drogon::authz
