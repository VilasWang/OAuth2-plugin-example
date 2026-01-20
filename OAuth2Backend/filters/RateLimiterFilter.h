#pragma once

#include <drogon/HttpFilter.h>

class RateLimiterFilter : public drogon::HttpFilter<RateLimiterFilter>
{
  public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fcc) override;
};
