#pragma once

// M3 Task 20 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/SessionController.h into
// authforge::drogon::controllers.

#include <drogon/HttpController.h>

// M3 Task 23 (authforge-sdk-refactor, evaluation H4): see
// HealthController.h's identical comment for the rationale.
class OAuth2Plugin;

// Task 24 slice 4 (authforge-sdk-refactor): forward-declared for the same
// reason as OAuth2Plugin above -- these are held as non-owning raw
// pointers (not shared_ptr), so a forward declaration is sufficient here
// and this header does not force every consumer (bootstrap/
// ControllerRegistration.cc, test files constructing SessionController
// directly) to pull in the full identity headers just to hold a pointer
// member. The actual instances are owned by bootstrap::wireIdentityServices()
// (OAuth2Server/bootstrap/IdentityAssembly.cc), which outlives every
// controller singleton -- same lifetime contract as OAuth2Plugin (owned by
// Drogon's PluginsManager).
namespace authforge::identity
{
class AuthService;
class SessionManager;
}  // namespace authforge::identity

namespace authforge::drogon::controllers
{

class SessionController : public ::drogon::HttpController<SessionController, false>
{
  public:
    // M3 Task 23: see HealthController::setPlugin()'s comment.
    void setPlugin(OAuth2Plugin *plugin)
    {
        plugin_ = plugin;
    }

    // Task 24 slice 4: identity-layer service injection, same
    // non-owning-raw-pointer + setter pattern as setPlugin() above. Each
    // handler below falls back to the pre-Task-24 legacy path
    // (authforge::drogon::services::AuthService / the inline CHECK 1/
    // CHECK 2 policy chain) when unset, mirroring resolvePlugin()'s
    // cached-pointer-with-fallback convention -- additive, not a
    // behavior-changing requirement.
    void setIdentityAuthService(authforge::identity::AuthService *authService)
    {
        identityAuthService_ = authService;
    }

    void setSessionManager(authforge::identity::SessionManager *sessionManager)
    {
        sessionManager_ = sessionManager;
    }

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SessionController::showLoginPage, "/login", ::drogon::Get);
    ADD_METHOD_TO(SessionController::login, "/oauth2/login", ::drogon::Post);
    ADD_METHOD_TO(SessionController::consent, "/oauth2/consent", ::drogon::Post);
    ADD_METHOD_TO(
      SessionController::logout,
      "/oauth2/logout",
      ::drogon::Post,
      "authforge::drogon::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(SessionController::registerUser, "/api/register", ::drogon::Post);
    METHOD_LIST_END

    void showLoginPage(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void login(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void consent(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void logout(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void registerUser(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

  private:
    OAuth2Plugin *plugin_ = nullptr;
    OAuth2Plugin *resolvePlugin() const;

    // Task 24 slice 4: see setIdentityAuthService()/setSessionManager()'s
    // comment above.
    authforge::identity::AuthService *identityAuthService_ = nullptr;
    authforge::identity::SessionManager *sessionManager_ = nullptr;
};

}  // namespace authforge::drogon::controllers
