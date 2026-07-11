#include "ControllerRegistration.h"
#include <drogon/drogon.h>
#include <drogon/DrClassMap.h>
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
#include <authforge/drogon/controllers/ClientAdminController.h>
#include <oauth2/filters/AuthorizationFilter.h>
#include <oauth2/filters/OAuth2AuthFilter.h>
#include <oauth2/plugin/OAuth2Plugin.h>

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
      std::make_shared<authforge::drogon::controllers::ClientAdminController>()
    );
    drogon::app().registerController(
      std::make_shared<oauth2::controllers::OAuth2StandardController>()
    );
}

void wireControllerPluginDependencies()
{
    // M3 Task 23: fetch the plugin exactly once (config-reflection
    // construction has completed by the time registerBeginningAdvice
    // callbacks run -- see this function's header comment for the
    // ordering requirement) and push it into every controller/filter that
    // exposes a setPlugin(). Using drogon::DrClassMap::getSingleInstance<T>()
    // rather than re-constructing each controller: registerController()
    // already called DrClassMap::setSingleInstance(ctrlPtr), so the
    // SAME instance drogon dispatches requests to is the one we mutate
    // here (a fresh std::make_shared<T>() would wire a different, unused
    // object).
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    if (!plugin)
    {
        LOG_ERROR << "wireControllerPluginDependencies: OAuth2Plugin not found; "
                     "controllers/filters will fall back to per-request "
                     "getPlugin<OAuth2Plugin>() lookups";
        return;
    }

    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::HealthController>()
      ->setPlugin(plugin);
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::DeviceAuthController>()
      ->setPlugin(plugin);
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::GitHubController>()
      ->setPlugin(plugin);
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::MfaController>()
      ->setPlugin(plugin);
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::SessionController>()
      ->setPlugin(plugin);
    drogon::DrClassMap::getSingleInstance<oauth2::controllers::OAuth2StandardController>()
      ->setPlugin(plugin);

    // Filters are looked up by the same by-name DrClassMap mechanism their
    // ADD_METHOD_TO string references use -- these are the OLD
    // oauth2::filters::{AuthorizationFilter,OAuth2AuthFilter} classes
    // (OAuth2Plugin/include/oauth2/filters/*.h), NOT the libs/drogon
    // copies (see PROGRESS.md's filter-vs-controller distinction: the
    // libs/drogon filter copies are not referenced by any ADD_METHOD_TO
    // string and are dead code for routing purposes).
    drogon::DrClassMap::getSingleInstance<oauth2::filters::AuthorizationFilter>()->setPlugin(
      plugin
    );
    drogon::DrClassMap::getSingleInstance<oauth2::filters::OAuth2AuthFilter>()->setPlugin(plugin);
}

}  // namespace bootstrap
