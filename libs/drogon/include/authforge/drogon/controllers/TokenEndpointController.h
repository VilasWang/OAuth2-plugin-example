#pragma once

// B10 / Task 45 (design.md §5.8): split out of the former
// OAuth2StandardController (token-lifecycle portion: token/introspect/revoke/
// userInfo). AutoCreation=false.

#include <drogon/HttpController.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>

#include <string>

namespace authforge::drogon::controllers
{

// Helper struct to hold client credentials and authentication scheme
struct ClientCredentials
{
    std::string clientId;
    std::string clientSecret;
    std::string authScheme;  // "Basic" if from Authorization header, empty otherwise
};

class TokenEndpointController : public ::drogon::HttpController<TokenEndpointController, false>
{
  public:
    static void initApiDocs();

    void setPlugin(::OAuth2Plugin *plugin)
    {
        plugin_ = plugin;
    }

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TokenEndpointController::token, "/oauth2/token", ::drogon::Post);
    ADD_METHOD_TO(
      TokenEndpointController::userInfo,
      "/oauth2/userinfo",
      ::drogon::Get,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      TokenEndpointController::introspect,
      "/oauth2/introspect",
      ::drogon::Post,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      TokenEndpointController::revoke,
      "/oauth2/revoke",
      ::drogon::Post,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    METHOD_LIST_END

    void token(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void userInfo(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void introspect(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void revoke(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

  private:
    static void initApiDocsImpl();

    static ::drogon::HttpResponsePtr createSuccessResponse();

    static ClientCredentials extractClientCredentials(const ::drogon::HttpRequestPtr &req);

    ::OAuth2Plugin *plugin_ = nullptr;

    ::OAuth2Plugin *resolvePlugin() const;
};

}  // namespace authforge::drogon::controllers
