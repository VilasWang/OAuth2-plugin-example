#include <fulla/drogon/controllers/PasswordResetController.h>
#include <fulla/drogon/services/PasswordResetService.h>
#include <fulla/drogon/observability/openapi/OpenApiGenerator.h>

#include <drogon/drogon.h>

namespace fulla::drogon::controllers
{

namespace
{
struct PasswordResetControllerDocs
{
    PasswordResetControllerDocs()
    {
        ::fulla::drogon::observability::openapi::EndpointInfo requestDocs;
        requestDocs.path = "/api/password-reset/request";
        requestDocs.method = "POST";
        requestDocs.summary = "Request Password Reset";
        requestDocs.description = "Request a password reset link to be sent via email.";
        requestDocs.tags = {"User Verification"};
        requestDocs.requiresAuth = false;
        ::fulla::drogon::observability::openapi::OpenApiGenerator::addEndpoint(requestDocs);

        ::fulla::drogon::observability::openapi::EndpointInfo confirmDocs;
        confirmDocs.path = "/api/password-reset/confirm";
        confirmDocs.method = "POST";
        confirmDocs.summary = "Confirm Password Reset";
        confirmDocs.description = "Confirm a password reset using the token sent via email.";
        confirmDocs.tags = {"User Verification"};
        confirmDocs.requiresAuth = false;
        ::fulla::drogon::observability::openapi::OpenApiGenerator::addEndpoint(confirmDocs);
    }
};

PasswordResetControllerDocs docs_;
}  // namespace

}  // namespace fulla::drogon::controllers

namespace fulla::drogon::controllers
{

// Task B5: business logic extracted to PasswordResetService.

void PasswordResetController::request(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    services::PasswordResetService::requestReset(req, sharedCb);
}

void PasswordResetController::confirm(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    services::PasswordResetService::confirmReset(req, sharedCb);
}

}  // namespace fulla::drogon::controllers
