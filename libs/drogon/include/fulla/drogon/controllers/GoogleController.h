#pragma once

// M3 Task 20 slice 4 (fulla-sdk-refactor): relocated from
// OAuth2Server/controllers/GoogleController.h into
// fulla::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>

#ifdef WITH_SOCIAL
// Task 24 slice 5 (fulla-sdk-refactor): see SessionController.h's
// identical forward-declaration comment.
namespace fulla::identity
{
class GoogleAuthService;
}  // namespace fulla::identity
#endif  // WITH_SOCIAL

namespace fulla::drogon::controllers
{

class GoogleController : public ::drogon::HttpController<GoogleController, false>
{
  public:
#ifdef WITH_SOCIAL
    // Task 24 slice 5: identity-layer service injection, same
    // non-owning-raw-pointer + setter pattern as SessionController's
    // setIdentityAuthService(). Falls back to the pre-Task-24
    // drogon::HttpClient-direct path when unset.
    void setGoogleAuthService(fulla::identity::GoogleAuthService *service)
    {
        googleAuthService_ = service;
    }
#endif  // WITH_SOCIAL

    METHOD_LIST_BEGIN
    // Endpoint to exchange Google code for User Info
    ADD_METHOD_TO(GoogleController::login, "/api/google/login", ::drogon::Post, ::drogon::Options);
    METHOD_LIST_END

    void login(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

  private:
#ifdef WITH_SOCIAL
    fulla::identity::GoogleAuthService *googleAuthService_ = nullptr;
#endif  // WITH_SOCIAL
};

}  // namespace fulla::drogon::controllers
