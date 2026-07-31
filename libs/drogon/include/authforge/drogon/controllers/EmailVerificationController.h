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
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    METHOD_LIST_END

    // Task B5: business logic moved to
    // authforge::drogon::services::EmailVerificationService.

    void verify(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void resend(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace authforge::drogon::controllers
