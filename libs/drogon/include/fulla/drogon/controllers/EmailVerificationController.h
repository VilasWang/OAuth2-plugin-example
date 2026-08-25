#pragma once

// M3 Task 20 slice 7 (fulla-sdk-refactor): relocated from
// OAuth2Server/controllers/EmailVerificationController.h into
// fulla::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>

namespace fulla::drogon::controllers
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
      "fulla::drogon::filters::OAuth2AuthFilter"
    );
    METHOD_LIST_END

    // Task B5: business logic moved to
    // fulla::drogon::services::EmailVerificationService.

    void verify(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void resend(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace fulla::drogon::controllers
