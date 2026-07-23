#include <authforge/drogon/filters/OAuth2AuthFilter.h>
#include <oauth2/error/ErrorResponder.h>
#include <oauth2/error/ErrorTypes.h>
#include <oauth2/error/RequestId.h>
#include <drogon/drogon.h>

void authforge::drogon::filters::OAuth2AuthFilter::doFilter(
  const ::drogon::HttpRequestPtr &req,
  ::drogon::FilterCallback &&fcb,
  ::drogon::FilterChainCallback &&fccb
)
{
    auto plugin = ::drogon::app().getPlugin<OAuth2Plugin>();
    if (!plugin)
    {
        LOG_ERROR << "OAuth2AuthFilter: OAuth2Plugin not found";
        auto error = ::authforge::common::error::Error::fromCode(
          "INTERNAL_ERROR", ::authforge::common::error::RequestId::resolve(req)
        );
        error.message = "OAuth2 plugin not available";
        auto resp = ::authforge::common::error::ErrorResponder::buildResponse(req, error);
        fcb(resp);
        return;
    }

    if (req->method() == ::drogon::Options)
    {
        fccb();
        return;
    }

    auto authHeader = req->getHeader("Authorization");
    if (authHeader.empty() || authHeader.substr(0, 7) != "Bearer ")
    {
        LOG_WARN << "OAuth2AuthFilter: Missing or invalid Authorization header";
        auto error = ::authforge::common::error::Error::fromCode(
          "AUTH_TOKEN_INVALID", ::authforge::common::error::RequestId::resolve(req)
        );
        error.message = "Missing or invalid Authorization header";
        auto resp = ::authforge::common::error::ErrorResponder::buildResponse(req, error);
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
              auto error = ::authforge::common::error::Error::fromCode(
                "AUTH_TOKEN_INVALID", ::authforge::common::error::RequestId::resolve(req)
              );
              error.message = "Invalid or expired token";
              auto resp = ::authforge::common::error::ErrorResponder::buildResponse(req, error);
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
