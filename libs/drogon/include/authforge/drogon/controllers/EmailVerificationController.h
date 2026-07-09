#pragma once

// M3 Task 20 slice 7 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/EmailVerificationController.h into
// authforge::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

class EmailVerificationController
  : public ::drogon::HttpController<EmailVerificationController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(EmailVerificationController::verify, "/api/verify-email", ::drogon::Get);
    ADD_METHOD_TO(
      EmailVerificationController::resend,
      "/api/verify-email/resend",
      ::drogon::Post,
      "oauth2::filters::OAuth2AuthFilter"
    );
    METHOD_LIST_END

    void verify(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void resend(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    // Helper: send verification email for a user
    static void sendVerificationEmail(int userId, const std::string &email);
};

}  // namespace authforge::drogon::controllers
