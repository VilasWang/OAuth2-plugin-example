#pragma once

// B10 / Task 45 (authforge-sdk-refactor, design.md §5.8): split out of the
// former OAuth2StandardController (authorize portion). AutoCreation=false +
// explicit registerController (§5.5); plugin pointer via setPlugin() +
// resolvePlugin() fallback (HealthController pattern).

#include <drogon/HttpController.h>
#include <oauth2/plugin/OAuth2Plugin.h>

namespace authforge::drogon::controllers
{

class AuthorizationEndpointController
    : public ::drogon::HttpController<AuthorizationEndpointController, false>
{
  public:
    static void initApiDocs();

    void setPlugin(::OAuth2Plugin *plugin)
    {
        plugin_ = plugin;
    }

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthorizationEndpointController::authorize, "/oauth2/authorize", ::drogon::Get);
    METHOD_LIST_END

    void authorize(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

  private:
    static void initApiDocsImpl();

    ::OAuth2Plugin *plugin_ = nullptr;

    ::OAuth2Plugin *resolvePlugin() const;
};

}  // namespace authforge::drogon::controllers
