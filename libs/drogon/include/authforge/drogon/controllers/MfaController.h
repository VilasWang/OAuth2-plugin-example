#pragma once

// M3 Task 20 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/MfaController.h into authforge::drogon::controllers.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

class MfaController : public ::drogon::HttpController<MfaController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(
      MfaController::setup, "/api/me/mfa/setup", ::drogon::Post, "oauth2::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      MfaController::verifySetup,
      "/api/me/mfa/verify",
      ::drogon::Post,
      "oauth2::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      MfaController::disable,
      "/api/me/mfa/disable",
      ::drogon::Post,
      "oauth2::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(MfaController::verifyLogin, "/oauth2/mfa/verify", ::drogon::Post);
    METHOD_LIST_END

    void setup(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void verifySetup(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void disable(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void verifyLogin(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace authforge::drogon::controllers
