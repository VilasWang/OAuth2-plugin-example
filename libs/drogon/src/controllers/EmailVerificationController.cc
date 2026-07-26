#include <authforge/drogon/controllers/EmailVerificationController.h>
#include <authforge/drogon/services/EmailVerificationService.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>

#include <drogon/drogon.h>

namespace authforge::drogon::controllers
{

namespace
{
struct EmailVerificationControllerDocs
{
    EmailVerificationControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo verifyEmail;
        verifyEmail.path = "/api/verify-email";
        verifyEmail.method = "GET";
        verifyEmail.summary = "Verify Email";
        verifyEmail.description = "Verify an email address using a token.";
        verifyEmail.tags = {"User Verification"};
        verifyEmail.requiresAuth = false;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(verifyEmail);

        ::authforge::drogon::observability::openapi::EndpointInfo resendEmail;
        resendEmail.path = "/api/verify-email/resend";
        resendEmail.method = "POST";
        resendEmail.summary = "Resend Verification Email";
        resendEmail.description = "Resend the email verification link.";
        resendEmail.tags = {"User Verification"};
        resendEmail.requiresAuth = false;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(resendEmail);
    }
};

EmailVerificationControllerDocs docs_;
}  // namespace

}  // namespace authforge::drogon::controllers

namespace authforge::drogon::controllers
{

// Task B5: business logic extracted to EmailVerificationService.
// The controller only parses the request and delegates to the service.

void EmailVerificationController::verify(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    services::EmailVerificationService::verifyToken(req, sharedCb);
}

void EmailVerificationController::resend(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    services::EmailVerificationService::resendVerification(req, sharedCb);
}

}  // namespace authforge::drogon::controllers
