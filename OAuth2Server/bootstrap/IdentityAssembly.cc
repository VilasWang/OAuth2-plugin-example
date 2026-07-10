#include "IdentityAssembly.h"

#include <authforge/drogon/controllers/SessionController.h>
#include <authforge/identity/AuthService.h>
#include <authforge/identity/IBackchannelLogoutNotifier.h>
#include <authforge/identity/SessionManager.h>
#include <authforge/storage/postgres/PostgresIdentityRepository.h>
#include <drogon/DrClassMap.h>
#include <drogon/drogon.h>
#include <oauth2/adapters/OpenSslCryptoProvider.h>
#include <oauth2/adapters/SystemClock.h>

#include <memory>

namespace bootstrap
{

namespace
{

// Task 24 slice 4: replaces
// libs/drogon/src/controllers/SessionController.cc's
// `sendBackchannelLogoutNotifications` stub (`LOG_DEBUG <<
// "sendBackchannelLogoutNotifications: stub";`) with a real
// IBackchannelLogoutNotifier implementation of identical behavior --
// IBackchannelLogoutNotifier.h's own top comment explicitly scopes a real
// OIDC back-channel-logout HTTP delivery implementation OUT of this task
// (deferred to a future Adapter-layer task), so this is a like-for-like
// port of the pre-existing stub onto the new port, not a behavior change.
class LoggingBackchannelLogoutNotifier : public authforge::identity::IBackchannelLogoutNotifier
{
  public:
    void notify(const std::string &userId, std::function<void()> &&callback) override
    {
        LOG_DEBUG << "sendBackchannelLogoutNotifications: stub (userId=" << userId << ")";
        callback();
    }
};

}  // namespace

void wireIdentityServices()
{
    auto dbClient = drogon::app().getDbClient();
    if (!dbClient)
    {
        LOG_WARN << "wireIdentityServices: no default DB client configured; "
                    "SessionController falls back to the legacy "
                    "authforge::drogon::services::AuthService path";
        return;
    }

    auto userRepo =
      std::make_shared<authforge::storage::postgres::PostgresIdentityRepository>(dbClient);
    auto crypto = std::make_shared<oauth2::adapters::OpenSslCryptoProvider>();
    auto clock = std::make_shared<oauth2::adapters::SystemClock>();

    // Owned for the lifetime of the process (same static-local-in-a-
    // free-function pattern OAuth2Plugin.cc's static jwkManagerLogger
    // uses) -- SessionController holds a non-owning raw pointer to each
    // (see SessionController.h's setIdentityAuthService()/
    // setSessionManager() comments), and this function's only caller
    // (main.cc's registerBeginningAdvice) runs exactly once at startup.
    static auto authService =
      std::make_shared<authforge::identity::AuthService>(userRepo, crypto, clock);
    static auto notifier = std::make_shared<LoggingBackchannelLogoutNotifier>();
    static auto sessionManager = std::make_shared<authforge::identity::SessionManager>(notifier);

    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::SessionController>()
      ->setIdentityAuthService(authService.get());
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::SessionController>()
      ->setSessionManager(sessionManager.get());

    LOG_INFO << "Identity services wired into SessionController "
                "(authforge::identity::AuthService + SessionManager)";
}

}  // namespace bootstrap
