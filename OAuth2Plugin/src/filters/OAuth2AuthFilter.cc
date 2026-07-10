#include <oauth2/filters/OAuth2AuthFilter.h>
#include <oauth2/error/ErrorResponder.h>
#include <oauth2/error/ErrorTypes.h>
#include <oauth2/error/RequestId.h>
#include <drogon/drogon.h>

OAuth2Plugin *oauth2::filters::OAuth2AuthFilter::resolvePlugin() const
{
    return plugin_ ? plugin_ : drogon::app().getPlugin<OAuth2Plugin>();
}

void oauth2::filters::OAuth2AuthFilter::doFilter(
  const HttpRequestPtr &req,
  FilterCallback &&fcb,
  FilterChainCallback &&fccb
)
{
    auto plugin = resolvePlugin();
    if (!plugin)
    {
        LOG_ERROR << "OAuth2AuthFilter: OAuth2Plugin not found";
        auto error =
          common::error::Error::fromCode("INTERNAL_ERROR", common::error::RequestId::resolve(req));
        error.message = "OAuth2 plugin not available";
        auto resp = common::error::ErrorResponder::buildResponse(req, error);
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
        auto error = common::error::Error::fromCode(
          "AUTH_TOKEN_INVALID", common::error::RequestId::resolve(req)
        );
        error.message = "Missing or invalid Authorization header";
        auto resp = common::error::ErrorResponder::buildResponse(req, error);
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
              auto error = common::error::Error::fromCode(
                "AUTH_TOKEN_INVALID", common::error::RequestId::resolve(req)
              );
              error.message = "Invalid or expired token";
              auto resp = common::error::ErrorResponder::buildResponse(req, error);
              fcb(resp);
              return;
          }

          // Success
          (*req->getAttributes())["userId"] = tokenInfo->userId;
          (*req->getAttributes())["scope"] = tokenInfo->scope;
          (*req->getAttributes())["clientId"] = tokenInfo->clientId;

          fccb();
      }
    );
}
