#pragma once

// M3 Task 20 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/MfaController.h into authforge::drogon::controllers.

#include <drogon/HttpController.h>

// M3 Task 23 (authforge-sdk-refactor, evaluation H4): see
// HealthController.h's identical comment for the rationale.
class OAuth2Plugin;

// Task 24 slice 5 (authforge-sdk-refactor): see SessionController.h's
// identical forward-declaration comment.
namespace authforge::identity
{
class MfaService;
class IUserRepository;
}  // namespace authforge::identity

namespace authforge::drogon::controllers
{

class MfaController : public ::drogon::HttpController<MfaController, false>
{
  public:
    // M3 Task 23: see HealthController::setPlugin()'s comment.
    void setPlugin(OAuth2Plugin *plugin)
    {
        plugin_ = plugin;
    }

    // Task 24 slice 5: identity-layer service injection, same
    // non-owning-raw-pointer + setter pattern as setPlugin() above. Each
    // handler below falls back to the pre-Task-24 raw-SQL path when
    // unset (see resolvePlugin()'s identical fallback convention).
    void setMfaService(authforge::identity::MfaService *mfaService)
    {
        mfaService_ = mfaService;
    }
    // Task 24 slice 5: needed to resolve the "userId" request attribute
    // (actually the OAuth2 public_sub, see IUserRepository::
    // findByPublicSub's header comment) into the internal id MfaService
    // is keyed by. A raw non-owning pointer to the shared instance
    // OAuth2Server/bootstrap/IdentityAssembly.cc already constructs for
    // SessionController's AuthService -- not a new object.
    void setUserRepository(authforge::identity::IUserRepository *userRepo)
    {
        userRepo_ = userRepo;
    }

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(
      MfaController::setup, "/api/me/mfa/setup", ::drogon::Post, "oauth2::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      MfaController::verifySetup,
      "/api/me/mfa/verify",
      ::drogon::Post,
      "oauth2::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      MfaController::disable,
      "/api/me/mfa/disable",
      ::drogon::Post,
      "oauth2::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(MfaController::verifyLogin, "/oauth2/mfa/verify", ::drogon::Post);
    METHOD_LIST_END

    void setup(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void verifySetup(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void disable(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void verifyLogin(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

  private:
    OAuth2Plugin *plugin_ = nullptr;
    OAuth2Plugin *resolvePlugin() const;

    // Task 24 slice 5: see setMfaService()/setUserRepository()'s comment
    // above.
    authforge::identity::MfaService *mfaService_ = nullptr;
    authforge::identity::IUserRepository *userRepo_ = nullptr;
};

}  // namespace authforge::drogon::controllers
