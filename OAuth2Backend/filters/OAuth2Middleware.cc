#include "OAuth2Middleware.h"
#include <drogon/drogon.h>

void OAuth2Middleware::doFilter(const HttpRequestPtr &req,
                                FilterCallback &&fcb,
                                FilterChainCallback &&fccb)
{
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    if (!plugin) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k500InternalServerError);
        fcb(resp);
        return;
    }

    if (req->method() == Options) {
        fccb();
        return;
    }

    auto authHeader = req->getHeader("Authorization");
    if (authHeader.empty() || authHeader.substr(0, 7) != "Bearer ") {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        resp->setBody("Missing or invalid Authorization header");
        fcb(resp);
        return;
    }

    std::string token = authHeader.substr(7);

    // Async Token Validation
    plugin->validateAccessToken(token, 
        [req, fcb = std::move(fcb), fccb = std::move(fccb)](std::shared_ptr<OAuth2Plugin::AccessToken> tokenInfo) {
            if (!tokenInfo) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k401Unauthorized);
                resp->setBody("Invalid or expired token");
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
