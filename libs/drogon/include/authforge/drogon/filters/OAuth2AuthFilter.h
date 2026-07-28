#pragma once

#include <drogon/HttpFilter.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>

using namespace drogon;

namespace authforge::drogon::filters
{

class OAuth2AuthFilter : public ::drogon::HttpFilter<OAuth2AuthFilter>
{
  public:
    OAuth2AuthFilter()
    {
    }

    // M3 Task 23 (authforge-sdk-refactor, evaluation H4): see
    // AuthorizationFilter::setPlugin()'s identical comment.
    void setPlugin(::OAuth2Plugin *plugin)
    {
        plugin_ = plugin;
    }

    void doFilter(
      const HttpRequestPtr &req,
      FilterCallback &&fcb,
      FilterChainCallback &&fccb
    ) override;

  private:
    ::OAuth2Plugin *plugin_ = nullptr;
    ::OAuth2Plugin *resolvePlugin() const;
};

}  // namespace authforge::drogon::filters
