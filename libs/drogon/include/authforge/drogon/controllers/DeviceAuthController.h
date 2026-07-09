#pragma once

// M3 Task 20 slice 7 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/DeviceAuthController.h into
// authforge::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

/**
 * @brief Device Authorization Grant Controller (RFC 8628)
 *
 * Implements the Device Authorization Grant flow for devices with limited input
 * capabilities (TVs, CLI tools, IoT devices). The device displays a user code
 * that the user enters on another device to authorize access.
 *
 * Endpoints:
 * - POST /oauth2/device_authorization — Device requests authorization
 * - POST /oauth2/device/approve — User approves the device (admin-only)
 */
class DeviceAuthController : public ::drogon::HttpController<DeviceAuthController, false>
{
  public:
    METHOD_LIST_BEGIN
    // Device Authorization Request (no auth required)
    ADD_METHOD_TO(
      DeviceAuthController::deviceAuthorization, "/oauth2/device_authorization", ::drogon::Post
    );

    // User Approval (admin-only)
    ADD_METHOD_TO(
      DeviceAuthController::approveDevice,
      "/oauth2/device/approve",
      ::drogon::Post,
      "oauth2::filters::AuthorizationFilter"
    );
    METHOD_LIST_END

    void deviceAuthorization(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void approveDevice(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

  private:
    /**
     * @brief Generate a user-friendly 8-character uppercase alphanumeric code
     */
    static std::string generateUserCode();
};

}  // namespace authforge::drogon::controllers
