#pragma once

#include <drogon/HttpController.h>
#include "../plugins/OAuth2Plugin.h"

using namespace drogon;

class OAuth2Controller : public drogon::HttpController<OAuth2Controller>
{
  public:
    METHOD_LIST_BEGIN
    // Authorization Endpoint
    // GET /oauth2/authorize
    ADD_METHOD_TO(OAuth2Controller::authorize, "/oauth2/authorize", Get);

    // Token Endpoint
    // POST /oauth2/token
    ADD_METHOD_TO(OAuth2Controller::token, "/oauth2/token", Post);

    // UserInfo Endpoint (Protected)
    // GET /oauth2/userinfo
    ADD_METHOD_TO(OAuth2Controller::userInfo,
                  "/oauth2/userinfo",
                  Get,
                  Options,
                  "OAuth2Middleware");

    // Login Form Submission (Internal)
    ADD_METHOD_TO(OAuth2Controller::login, "/oauth2/login", Post);

    // Register User (Helper for testing)
    // POST /api/register
    ADD_METHOD_TO(OAuth2Controller::registerUser, "/api/register", Post);

    METHOD_LIST_END

    void authorize(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

    void login(const HttpRequestPtr &req,
               std::function<void(const HttpResponsePtr &)> &&callback);

    void token(const HttpRequestPtr &req,
               std::function<void(const HttpResponsePtr &)> &&callback);

    void userInfo(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);

    void registerUser(const HttpRequestPtr &req,
                      std::function<void(const HttpResponsePtr &)> &&callback);
};
