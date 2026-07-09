#include "ControllerRegistration.h"
#include <drogon/drogon.h>
#include <authforge/drogon/controllers/OAuth2StandardController.h>
#include <authforge/drogon/controllers/HealthController.h>
#include <authforge/drogon/controllers/GoogleController.h>
#include <authforge/drogon/controllers/WeChatController.h>
#include <authforge/drogon/controllers/OrganizationController.h>
#include <authforge/drogon/controllers/ClientRegistrationController.h>
#include <authforge/drogon/controllers/ApiDocController.h>
#include <authforge/drogon/controllers/DeviceAuthController.h>
#include <authforge/drogon/controllers/EmailVerificationController.h>
#include <authforge/drogon/controllers/GitHubController.h>
#include <authforge/drogon/controllers/MfaController.h>
#include <authforge/drogon/controllers/PasswordResetController.h>
#include <authforge/drogon/controllers/SessionController.h>
#include <authforge/drogon/controllers/UserSelfServiceController.h>
#include <authforge/drogon/controllers/WebAuthnController.h>
#include <authforge/drogon/controllers/AdminController.h>

namespace bootstrap
{

void registerAllControllers()
{
    // M3 Task 20 (verified mechanism, see PROGRESS.md): every controller
    // below uses HttpController<T, false> (AutoCreation=false), so route
    // registration only happens via this explicit call chain -- there is
    // no static-initialization side effect to rely on. This establishes a
    // real, linker-visible reference into each controller's translation
    // unit inside the authforge-drogon static library, which is why a
    // PLAIN (non-whole-archive) link is sufficient.
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::HealthController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::GoogleController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::WeChatController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::OrganizationController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::ClientRegistrationController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::ApiDocController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::DeviceAuthController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::EmailVerificationController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::GitHubController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::MfaController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::PasswordResetController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::SessionController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::UserSelfServiceController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::WebAuthnController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::AdminController>()
    );
    drogon::app().registerController(
      std::make_shared<oauth2::controllers::OAuth2StandardController>()
    );
}

}  // namespace bootstrap
