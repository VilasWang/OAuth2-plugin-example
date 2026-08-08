#include <authforge/drogon/filters/OAuth2AuthFilter.h>
#include <authforge/drogon/error/ErrorResponder.h>
#include <authforge/common/error/ErrorTypes.h>
#include <authforge/drogon/error/RequestId.h>
#include <authforge/drogon/utils/ScopeChecker.h>
#include <drogon/drogon.h>

#include <string>
#include <string_view>

OAuth2Plugin *authforge::drogon::filters::OAuth2AuthFilter::resolvePlugin() const
{
    return plugin_ ? plugin_ : ::drogon::app().getPlugin<OAuth2Plugin>();
}

namespace
{
// F-010 (RFC 6750 §3.1 / RFC 6749 §3.3): minimal path -> required-scope map.
//
// Returns the required scope for a request path, or "" when no scope is
// required at this layer. This is the minimal resource-scope mapping called
// out in the OAuth/OIDC audit plan -- a full per-resource scope model is
// future work (tracked as the "完整资源-scope 授权模型" follow-up).
//
//   /oauth2/userinfo -> "openid"   (defense-in-depth: the userinfo handler
//                                   in TokenEndpointController.cc ALSO checks
//                                   this; keeping both per the plan's "your
//                                   call, but userinfo MUST end up requiring
//                                   openid through at least one path" -- the
//                                   filter is the cheaper rejection point and
//                                   keeps the WWW-Authenticate header uniform.)
//   /api/me and /api/me/* -> "profile"
//
// Path matching is prefix-based with explicit boundary checks so that
// `/api/measurement` does NOT match the `/api/me` rule.
std::string requiredScopeForPath(std::string_view path)
{
    // /oauth2/userinfo (exact). Other /oauth2/* endpoints (token, introspect,
    // revoke, authorize, login) are protocol endpoints that do their own
    // client authentication and must NOT be gated by a user-scope here.
    if (path == "/oauth2/userinfo")
        return "openid";

    // /api/me (exact) or /api/me/<...>
    if (path == "/api/me" ||
        (path.size() > sizeof("/api/me") && path.compare(0, sizeof("/api/me"), "/api/me") == 0 &&
         path[sizeof("/api/me") - 1] == '/'))
    {
        return "profile";
    }

    return "";
}

// F-010: builds the RFC 6750 §3.1 WWW-Authenticate challenge for an
// insufficient_scope rejection. The `scope` attribute names the scope the
// client would need to retry successfully.
std::string insufficientScopeChallenge(const std::string &requiredScope)
{
    return "Bearer realm=\"authforge\", error=\"insufficient_scope\", "
           "error_description=\"The access token does not have the required scope\", "
           "scope=\"" +
           requiredScope + "\"";
}
}  // namespace

void authforge::drogon::filters::OAuth2AuthFilter::doFilter(
  const HttpRequestPtr &req,
  FilterCallback &&fcb,
  FilterChainCallback &&fccb
)
{
    try
    {
        auto plugin = resolvePlugin();
        if (!plugin)
        {
            LOG_ERROR << "OAuth2AuthFilter: OAuth2Plugin not found";
            auto error = authforge::common::error::Error::fromCode(
              "INTERNAL_ERROR", authforge::common::error::RequestId::resolve(req)
            );
            error.message = "OAuth2 plugin not available";
            auto resp = authforge::common::error::ErrorResponder::buildResponse(req, error);
            fcb(resp);
            return;
        }

        if (req->method() == Options)
        {
            fccb();
            return;
        }

        auto authHeader = req->getHeader("Authorization");
        if (authHeader.empty() || authHeader.substr(0, 7) != "Bearer ")
        {
            LOG_WARN << "OAuth2AuthFilter: Missing or invalid Authorization header";
            auto error = authforge::common::error::Error::fromCode(
              "AUTH_TOKEN_INVALID", authforge::common::error::RequestId::resolve(req)
            );
            error.message = "Missing or invalid Authorization header";
            auto resp = authforge::common::error::ErrorResponder::buildResponse(req, error);
            fcb(resp);
            return;
        }

        std::string token = authHeader.substr(7);

        // Async Token Validation
        plugin->validateAccessToken(
          token,
          [req,
           fcb = std::move(fcb),
           fccb = std::move(fccb)](std::shared_ptr<OAuth2Plugin::AccessToken> tokenInfo) {
              if (!tokenInfo)
              {
                  LOG_WARN << "OAuth2AuthFilter: Token validation failed";
                  auto error = authforge::common::error::Error::fromCode(
                    "AUTH_TOKEN_INVALID", authforge::common::error::RequestId::resolve(req)
                  );
                  error.message = "Invalid or expired token";
                  auto resp = authforge::common::error::ErrorResponder::buildResponse(req, error);
                  // F-006 (RFC 6750 §3): the request carried Bearer
                  // credentials that failed validation, so the 401 MUST carry
                  // a WWW-Authenticate challenge with error="invalid_token".
                  // (The no-credentials branch above intentionally sends none.)
                  resp->addHeader(
                    "WWW-Authenticate",
                    "Bearer realm=\"authforge\", error=\"invalid_token\", "
                    "error_description=\"Invalid or expired token\""
                  );
                  fcb(resp);
                  return;
              }

              // Success: token validated. Persist the principal for the
              // downstream handler.
              (*req->getAttributes())["userId"] = tokenInfo->userId;
              (*req->getAttributes())["scope"] = tokenInfo->scope;
              (*req->getAttributes())["clientId"] = tokenInfo->clientId;

              // F-010 (RFC 6750 §3.1): enforce the minimal path -> required-
              // scope map. If the token's scope does not include the required
              // scope for this path, reject with 403 insufficient_scope +
              // WWW-Authenticate challenge (RFC 6750 §3.1 REQUIRES the
              // `scope` attribute in that challenge naming the missing scope).
              // Parsed here (not earlier) so a token that fails validation is
              // reported as 401 invalid_token, not 403 insufficient_scope --
              // the order is: prove the bearer is valid, then prove it is
              // scoped for the resource.
              auto requiredScope = requiredScopeForPath(req->path());
              if (!requiredScope.empty() &&
                  !authforge::drogon::utils::hasScope(tokenInfo->scope, requiredScope))
              {
                  LOG_WARN << "OAuth2AuthFilter: insufficient_scope for path "
                           << req->path() << " (requires '" << requiredScope << "')";
                  auto error = authforge::common::error::Error::fromCode(
                    "AUTHZ_INSUFFICIENT_PERMISSIONS",
                    authforge::common::error::RequestId::resolve(req)
                  );
                  error.message = "Insufficient scope for this resource";
                  auto resp =
                    authforge::common::error::ErrorResponder::buildResponse(req, error);
                  // RFC 6750 §3.1: the challenge carries error="insufficient_scope"
                  // AND a `scope` attribute naming the scope(s) that would unlock
                  // the resource so the client can re-authorize for it.
                  resp->addHeader("WWW-Authenticate", insufficientScopeChallenge(requiredScope));
                  fcb(resp);
                  return;
              }

              fccb();
          }
        );
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "OAuth2AuthFilter: Unhandled exception: " << e.what();
        auto error = authforge::common::error::Error::fromCode(
          "INTERNAL_ERROR", authforge::common::error::RequestId::resolve(req)
        );
        error.message = "Internal filter error";
        auto resp = authforge::common::error::ErrorResponder::buildResponse(req, error);
        fcb(resp);
    }
    catch (...)
    {
        LOG_ERROR << "OAuth2AuthFilter: Unknown exception";
        auto error = authforge::common::error::Error::fromCode(
          "INTERNAL_ERROR", authforge::common::error::RequestId::resolve(req)
        );
        error.message = "Internal filter error";
        auto resp = authforge::common::error::ErrorResponder::buildResponse(req, error);
        fcb(resp);
    }
}
