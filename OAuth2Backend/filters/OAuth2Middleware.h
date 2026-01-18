#pragma once

#include <drogon/HttpFilter.h>
#include "../plugins/OAuth2Plugin.h"

using namespace drogon;

class OAuth2Middleware : public HttpFilter<OAuth2Middleware>
{
  public:
    OAuth2Middleware()
    {
    }

    void doFilter(const HttpRequestPtr &req,
                  FilterCallback &&fcb,
                  FilterChainCallback &&fccb) override;
};
