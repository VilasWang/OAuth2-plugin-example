#pragma once

// M3 Task 20 slice 7 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/UserSelfServiceController.h into
// authforge::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

class UserSelfServiceController : public ::drogon::HttpController<UserSelfServiceController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(
      UserSelfServiceController::getProfile,
      "/api/me",
      ::drogon::Get,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      UserSelfServiceController::changePassword,
      "/api/me/password",
      ::drogon::Put,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      UserSelfServiceController::listAuthorizedApps,
      "/api/me/authorized-apps",
      ::drogon::Get,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      UserSelfServiceController::revokeAuthorizedApp,
      "/api/me/authorized-apps/{clientId}",
      ::drogon::Delete,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      UserSelfServiceController::deleteAccount,
      "/api/me",
      ::drogon::Delete,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    METHOD_LIST_END

    void getProfile(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void changePassword(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void listAuthorizedApps(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void revokeAuthorizedApp(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &clientId
    );
    void deleteAccount(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace authforge::drogon::controllers
