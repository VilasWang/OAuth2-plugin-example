#pragma once

// B10 / Task 45 (design.md §5.8): split out of the former
// OAuth2StandardController (OIDC/OAuth2 discovery portion). AutoCreation=false.

#include <drogon/HttpController.h>
#include <fulla/drogon/plugin/OAuth2Plugin.h>

namespace fulla::drogon::controllers
{

class DiscoveryController : public ::drogon::HttpController<DiscoveryController, false>
{
  public:
    static void initApiDocs();

    void setPlugin(::OAuth2Plugin *plugin)
    {
        plugin_ = plugin;
    }

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(
      DiscoveryController::metadata,
      "/.well-known/oauth-authorization-server",
      ::drogon::Get
    );
    ADD_METHOD_TO(
      DiscoveryController::oidcDiscovery,
      "/.well-known/openid-configuration",
      ::drogon::Get
    );
    ADD_METHOD_TO(DiscoveryController::jwks, "/.well-known/jwks.json", ::drogon::Get);
    METHOD_LIST_END

    void metadata(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void oidcDiscovery(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void jwks(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

  private:
    static void initApiDocsImpl();

    ::OAuth2Plugin *plugin_ = nullptr;

    ::OAuth2Plugin *resolvePlugin() const;
};

}  // namespace fulla::drogon::controllers
