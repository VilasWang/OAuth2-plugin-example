#pragma once

// M3 Task 20 slice 7 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/ClientRegistrationController.h into
// authforge::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

/**
 * @brief Dynamic Client Registration Controller (RFC 7591)
 *
 * Provides a REST API for programmatic OAuth2 client registration.
 * Endpoint: POST /oauth2/register
 *
 * Access control: Requires Bearer token with admin role (AuthorizationFilter).
 */
class ClientRegistrationController
    : public ::drogon::HttpController<ClientRegistrationController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(
      ClientRegistrationController::registerClient,
      "/oauth2/register",
      ::drogon::Post,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    METHOD_LIST_END

    void registerClient(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace authforge::drogon::controllers
