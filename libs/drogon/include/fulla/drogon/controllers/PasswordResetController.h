#pragma once

// M3 Task 20 (fulla-sdk-refactor): relocated from
// OAuth2Server/controllers/PasswordResetController.h into
// fulla::drogon::controllers.

#include <drogon/HttpController.h>

namespace fulla::drogon::controllers
{

class PasswordResetController : public ::drogon::HttpController<PasswordResetController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(PasswordResetController::request, "/api/password-reset/request", ::drogon::Post);
    ADD_METHOD_TO(PasswordResetController::confirm, "/api/password-reset/confirm", ::drogon::Post);
    METHOD_LIST_END

    void request(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void confirm(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace fulla::drogon::controllers
