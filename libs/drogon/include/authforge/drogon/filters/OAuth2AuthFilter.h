#pragma once

// M3 Task 20 slice 2 (authforge-sdk-refactor): relocated verbatim from
// OAuth2Plugin/include/oauth2/filters/OAuth2AuthFilter.h into
// authforge::drogon::filters. Unlike slice 1's RequestValidationFilter,
// this filter DOES reach through the OAuth2Plugin singleton
// (drogon::app().getPlugin<OAuth2Plugin>()) -- acceptable here because
// libs/drogon already carries a temporary compile-time dependency on
// OAuth2Plugin since slice 1 (for the not-yet-relocated authforge::common::error
// machinery; see libs/drogon/CMakeLists.txt's header comment), so this
// does not introduce a NEW dependency edge, only exercises the existing
// one. The plugin itself is not being touched in this slice (still
// OAuth2Plugin, still the same class); Task 21/23 (plugin registration
// decision / de-singletonization) address the plugin's own future, not
// this filter's relocation.

#include <drogon/HttpFilter.h>
#include <oauth2/plugin/OAuth2Plugin.h>

namespace authforge::drogon::filters
{

class OAuth2AuthFilter : public ::drogon::HttpFilter<OAuth2AuthFilter>
{
  public:
    OAuth2AuthFilter()
    {
    }

    void doFilter(
      const ::drogon::HttpRequestPtr &req,
      ::drogon::FilterCallback &&fcb,
      ::drogon::FilterChainCallback &&fccb
    ) override;
};

}  // namespace authforge::drogon::filters
