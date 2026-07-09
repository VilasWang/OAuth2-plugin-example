#pragma once

// M3 Task 20 slice 7 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/WebAuthnController.h into
// authforge::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

/**
 * @brief WebAuthn / Passkey Controller
 *
 * Provides endpoints for passwordless authentication using WebAuthn.
 * Registration: user registers a new credential (passkey)
 * Authentication: user authenticates with an existing credential
 */
class WebAuthnController : public ::drogon::HttpController<WebAuthnController, false>
{
  public:
    METHOD_LIST_BEGIN
    // Registration flow (requires existing auth)
    ADD_METHOD_TO(
      WebAuthnController::registerBegin,
      "/api/me/webauthn/register/begin",
      ::drogon::Post,
      "oauth2::filters::OAuth2AuthFilter"
    );
    ADD_METHOD_TO(
      WebAuthnController::registerFinish,
      "/api/me/webauthn/register/finish",
      ::drogon::Post,
      "oauth2::filters::OAuth2AuthFilter"
    );
    // Authentication flow (no auth required - this IS the auth)
    ADD_METHOD_TO(
      WebAuthnController::authenticateBegin,
      "/oauth2/webauthn/authenticate/begin",
      ::drogon::Post
    );
    ADD_METHOD_TO(
      WebAuthnController::authenticateFinish,
      "/oauth2/webauthn/authenticate/finish",
      ::drogon::Post
    );
    // List credentials (requires auth)
    ADD_METHOD_TO(
      WebAuthnController::listCredentials,
      "/api/me/webauthn/credentials",
      ::drogon::Get,
      "oauth2::filters::OAuth2AuthFilter"
    );
    METHOD_LIST_END

    void registerBegin(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void registerFinish(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void authenticateBegin(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void authenticateFinish(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void listCredentials(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace authforge::drogon::controllers
