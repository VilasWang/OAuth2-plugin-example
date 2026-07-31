#pragma once

// M3 Task 20 slice 5 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/WeChatController.h into
// authforge::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>

#ifdef WITH_SOCIAL
// Task 24 slice 5 (authforge-sdk-refactor): see SessionController.h's
// identical forward-declaration comment.
namespace authforge::identity
{
class WeChatAuthService;
}  // namespace authforge::identity
#endif  // WITH_SOCIAL

namespace authforge::drogon::controllers
{

class WeChatController : public ::drogon::HttpController<WeChatController, false>
{
  public:
#ifdef WITH_SOCIAL
    // Task 24 slice 5: identity-layer service injection, same pattern as
    // GoogleController::setGoogleAuthService(). Falls back to the
    // pre-Task-24 drogon::HttpClient-direct path when unset.
    void setWeChatAuthService(authforge::identity::WeChatAuthService *service)
    {
        weChatAuthService_ = service;
    }
#endif  // WITH_SOCIAL

    METHOD_LIST_BEGIN
    // Endpoint to exchange WeChat code for User Info
    ADD_METHOD_TO(WeChatController::login, "/api/wechat/login", ::drogon::Post, ::drogon::Options);
    METHOD_LIST_END

    void login(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

  private:
#ifdef WITH_SOCIAL
    authforge::identity::WeChatAuthService *weChatAuthService_ = nullptr;
#endif  // WITH_SOCIAL
};

}  // namespace authforge::drogon::controllers
