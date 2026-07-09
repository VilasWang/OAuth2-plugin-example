#pragma once

// M3 Task 20 slice 5 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/WeChatController.h into
// authforge::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

class WeChatController : public ::drogon::HttpController<WeChatController, false>
{
  public:
    METHOD_LIST_BEGIN
    // Endpoint to exchange WeChat code for User Info
    ADD_METHOD_TO(
      WeChatController::login, "/api/wechat/login", ::drogon::Post, ::drogon::Options
    );
    METHOD_LIST_END

    void login(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace authforge::drogon::controllers
