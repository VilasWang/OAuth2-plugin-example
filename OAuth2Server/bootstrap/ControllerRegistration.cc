#include "ControllerRegistration.h"
#include <drogon/drogon.h>
#include <drogon/DrClassMap.h>
#include <authforge/drogon/controllers/AuthorizationEndpointController.h>
#include <authforge/drogon/controllers/TokenEndpointController.h>
#include <authforge/drogon/controllers/DiscoveryController.h>
#include <authforge/drogon/controllers/HealthController.h>
#ifdef WITH_SOCIAL
#include <authforge/drogon/controllers/GoogleController.h>
#include <authforge/drogon/controllers/WeChatController.h>
#endif  // WITH_SOCIAL
// M5 Task 30: OrganizationController moved to the product app
// (apps/server/src/organization/, namespace `organization`).
#include <OrganizationController.h>
#include <authforge/drogon/controllers/ClientRegistrationController.h>
#include <authforge/drogon/controllers/ApiDocController.h>
#include <authforge/drogon/controllers/DeviceAuthController.h>
#include <authforge/drogon/controllers/EmailVerificationController.h>
#ifdef WITH_SOCIAL
#include <authforge/drogon/controllers/GitHubController.h>
#endif  // WITH_SOCIAL
#include <authforge/drogon/controllers/MfaController.h>
#include <authforge/drogon/controllers/PasswordResetController.h>
#include <authforge/drogon/controllers/SessionController.h>
#include <authforge/drogon/controllers/UserSelfServiceController.h>
#ifdef WITH_WEBAUTHN
#include <authforge/drogon/controllers/WebAuthnController.h>
#endif  // WITH_WEBAUTHN
#include <authforge/drogon/controllers/ClientAdminController.h>
#include <authforge/drogon/controllers/UserAdminController.h>
#include <authforge/drogon/controllers/RoleScopeAdminController.h>
#include <authforge/drogon/controllers/TokenAdminController.h>
#include <authforge/drogon/controllers/AuditController.h>
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
#ifdef WITH_SOCIAL
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::GoogleController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::WeChatController>()
    );
#endif  // WITH_SOCIAL
    drogon::app().registerController(std::make_shared<::organization::OrganizationController>());
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
#ifdef WITH_SOCIAL
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::GitHubController>()
    );
#endif  // WITH_SOCIAL
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
#ifdef WITH_WEBAUTHN
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::WebAuthnController>()
    );
#endif  // WITH_WEBAUTHN
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::ClientAdminController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::UserAdminController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::RoleScopeAdminController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::TokenAdminController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::AuditController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::AuthorizationEndpointController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::TokenEndpointController>()
    );
    drogon::app().registerController(
      std::make_shared<authforge::drogon::controllers::DiscoveryController>()
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
#ifdef WITH_SOCIAL
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::GitHubController>()
      ->setPlugin(plugin);
#endif  // WITH_SOCIAL
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::MfaController>()
      ->setPlugin(plugin);
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::SessionController>()
      ->setPlugin(plugin);
    drogon::DrClassMap::getSingleInstance<
      authforge::drogon::controllers::AuthorizationEndpointController>()
      ->setPlugin(plugin);
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::TokenEndpointController>()
      ->setPlugin(plugin);
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::DiscoveryController>()
      ->setPlugin(plugin);

    // Filters are looked up by the same by-name DrClassMap mechanism their
    // ADD_METHOD_TO string references use -- these are the OLD
    // authforge::drogon::filters::{AuthorizationFilter,OAuth2AuthFilter} classes
    // (OAuth2Plugin/include/oauth2/filters/*.h), NOT the libs/drogon
    // copies (see PROGRESS.md's filter-vs-controller distinction: the
    // libs/drogon filter copies are not referenced by any ADD_METHOD_TO
    // string and are dead code for routing purposes).
    drogon::DrClassMap::getSingleInstance<authforge::drogon::filters::AuthorizationFilter>()
      ->setPlugin(plugin);
    drogon::DrClassMap::getSingleInstance<authforge::drogon::filters::OAuth2AuthFilter>()
      ->setPlugin(plugin);
}

}  // namespace bootstrap
