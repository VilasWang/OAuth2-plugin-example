#include "OAuth2Middleware.h"
#include <drogon/drogon.h>

void OAuth2Middleware::doFilter(const HttpRequestPtr &req,
                                FilterCallback &&fcb,
                                FilterChainCallback &&fccb)
{
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    if (!plugin)
    {
        LOG_ERROR << "OAuth2Plugin not loaded!";
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k500InternalServerError);
        fcb(resp);
        return;
    }

    // Allow OPTIONS requests (Preflight)
    if (req->method() == Options)
    {
        fccb();
        return;
    }

    auto authHeader = req->getHeader("Authorization");
    if (authHeader.empty() || authHeader.substr(0, 7) != "Bearer ")
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        resp->setBody("Missing or invalid Authorization header");
        fcb(resp);
        return;
    }

    std::string token = authHeader.substr(7);
    auto tokenInfo = plugin->validateAccessToken(token);

    if (!tokenInfo)
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        resp->setBody("Invalid or expired token");
        fcb(resp);
        return;
    }

    // Attach user info to request for the controller to use
    // In Drogon, we can use attributes or a custom context object.
    // Here we just set attributes for simplicity.
    (*req->getAttributes())["userId"] = tokenInfo->userId;
    (*req->getAttributes())["scope"] = tokenInfo->scope;
    (*req->getAttributes())["clientId"] = tokenInfo->clientId;

    fccb();
}
