#pragma once

// M3 Task 20 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/SessionController.h into
// authforge::drogon::controllers.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

class SessionController : public ::drogon::HttpController<SessionController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SessionController::showLoginPage, "/login", ::drogon::Get);
    ADD_METHOD_TO(SessionController::login, "/oauth2/login", ::drogon::Post);
    ADD_METHOD_TO(SessionController::consent, "/oauth2/consent", ::drogon::Post);
    ADD_METHOD_TO(
      SessionController::logout,
      "/oauth2/logout",
      ::drogon::Post,
      "oauth2::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(SessionController::registerUser, "/api/register", ::drogon::Post);
    METHOD_LIST_END

    void showLoginPage(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void login(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void consent(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void logout(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void registerUser(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace authforge::drogon::controllers
