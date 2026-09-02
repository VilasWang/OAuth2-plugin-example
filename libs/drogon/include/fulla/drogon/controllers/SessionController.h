#pragma once

// M3 Task 20 (fulla-sdk-refactor): relocated from
// OAuth2Server/controllers/SessionController.h into
// fulla::drogon::controllers.

#include <drogon/HttpController.h>

// M3 Task 23 (fulla-sdk-refactor, evaluation H4): see
// HealthController.h's identical comment for the rationale.
class OAuth2Plugin;

// Task 24 slice 4 (fulla-sdk-refactor): forward-declared for the same
// reason as OAuth2Plugin above -- these are held as non-owning raw
// pointers (not shared_ptr), so a forward declaration is sufficient here
// and this header does not force every consumer (bootstrap/
// ControllerRegistration.cc, test files constructing SessionController
// directly) to pull in the full identity headers just to hold a pointer
// member. The actual instances are owned by bootstrap::wireIdentityServices()
// (OAuth2Server/bootstrap/IdentityAssembly.cc), which outlives every
// controller singleton -- same lifetime contract as OAuth2Plugin (owned by
// Drogon's PluginsManager).
namespace fulla::identity
{
class AuthService;
class SessionManager;
}  // namespace fulla::identity

namespace fulla::drogon::controllers
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
    // (fulla::drogon::services::AuthService / the inline CHECK 1/
    // CHECK 2 policy chain) when unset, mirroring resolvePlugin()'s
    // cached-pointer-with-fallback convention -- additive, not a
    // behavior-changing requirement.
    void setIdentityAuthService(fulla::identity::AuthService *authService)
    {
        identityAuthService_ = authService;
    }

    void setSessionManager(fulla::identity::SessionManager *sessionManager)
    {
        sessionManager_ = sessionManager;
    }

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SessionController::showLoginPage, "/login", ::drogon::Get);
    ADD_METHOD_TO(SessionController::login, "/oauth2/login", ::drogon::Post);
    ADD_METHOD_TO(SessionController::consent, "/oauth2/consent", ::drogon::Post);
    // #145: session-authenticated forced-password-change endpoint. No auth
    // filter -- like /oauth2/login it authenticates via the browser session
    // and is only usable while the session carries the must_change_password
    // marker (set at login from the users row); old_password is always
    // verified against the stored hash.
    ADD_METHOD_TO(
      SessionController::changePasswordForced,
      "/oauth2/password/change",
      ::drogon::Post
    );
    ADD_METHOD_TO(
      SessionController::logout,
      "/oauth2/logout",
      ::drogon::Post,
      "fulla::drogon::filters::OAuth2AuthFilter"
    );
    // F-027 (OIDC RP-Initiated Logout 1.0): GET + POST so RP form-posts and
    // link-based logout both work; no auth filter -- the endpoint is reachable
    // unauthenticated (it terminates whatever session is present, if any).
    ADD_METHOD_TO(SessionController::endSession, "/oauth2/end_session", ::drogon::Get);
    ADD_METHOD_TO(SessionController::endSession, "/oauth2/end_session", ::drogon::Post);
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
    void changePasswordForced(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void logout(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void endSession(
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
    fulla::identity::AuthService *identityAuthService_ = nullptr;
    fulla::identity::SessionManager *sessionManager_ = nullptr;
};

}  // namespace fulla::drogon::controllers
