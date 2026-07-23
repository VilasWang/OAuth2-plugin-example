#include "IdentityAssembly.h"

#include <authforge/drogon/adapters/DrogonOAuthHttpClient.h>
#ifdef WITH_SOCIAL
#include <authforge/drogon/controllers/GitHubController.h>
#include <authforge/drogon/controllers/GoogleController.h>
#include <authforge/drogon/controllers/WeChatController.h>
#endif  // WITH_SOCIAL
#include <authforge/drogon/controllers/MfaController.h>
#include <authforge/drogon/controllers/SessionController.h>
#ifdef WITH_WEBAUTHN
#include <authforge/drogon/controllers/WebAuthnController.h>
#endif  // WITH_WEBAUTHN
#include <authforge/identity/AuthService.h>
#include <authforge/identity/IBackchannelLogoutNotifier.h>
#include <authforge/identity/IMfaRepository.h>
#include <authforge/identity/IWebAuthnRepository.h>
#include <authforge/identity/MfaService.h>
#include <authforge/identity/SessionManager.h>
#include <authforge/identity/SocialAuthService.h>
#include <authforge/identity/WebAuthnService.h>
#include <authforge/storage/postgres/PostgresIdentityRepository.h>
#include <authforge/storage/postgres/PostgresMfaRepository.h>
#include <authforge/storage/postgres/PostgresSocialAccountRepository.h>
#include <authforge/storage/postgres/PostgresWebAuthnRepository.h>
#include <drogon/DrClassMap.h>
#include <drogon/drogon.h>
#include <oauth2/adapters/OpenSslCryptoProvider.h>
#include <oauth2/adapters/SystemClock.h>
#include <oauth2/plugin/OAuth2Plugin.h>

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
    // Memory-only config (e.g. config.ci.json: "storage_type":"memory",
    // "db_clients":[]) has NO default DbClient. Drogon's
    // DbClientManager::getDbClient() hits a hard `assert(dbClientsMap_.find(
    // name) != dbClientsMap_.end())` -- a process-terminating assert, NOT a
    // catchable throw -- so the `if (!dbClient)` check below would never be
    // reached. Guard with the storage-type check BEFORE the getDbClient()
    // call (same pattern as ContractFixtures.h::getPostgresClientOrNull()).
    // SessionController/Mfa/WebAuthn/Social then stay on their legacy
    // (non-injected) fallback paths, as already documented for the no-DB
    // case. This also fixes a real memory-storage server startup crash, not
    // just the test leg.
    auto plugin = drogon::app().getPlugin<::OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        LOG_WARN << "wireIdentityServices: memory storage (no DB client) -- "
                    "SessionController/Mfa/WebAuthn/Social stay on legacy paths";
        return;
    }

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
    auto crypto = std::make_shared<authforge::drogon::adapters::OpenSslCryptoProvider>();
    auto clock = std::make_shared<authforge::drogon::adapters::SystemClock>();

    // Task 24 slice 5: MfaService, sharing the same IUserRepository/
    // ICryptoProvider/IClock instances constructed above (MfaService only
    // needs its own IMfaRepository -- a distinct interface/table-column
    // scope, see IMfaRepository.h's header comment -- not a new crypto/
    // clock).
    auto mfaRepo = std::make_shared<authforge::storage::postgres::PostgresMfaRepository>(dbClient);

    // custom_config is read by both WebAuthn (rp_id/rp_name) and Social
    // (external_auth.*) config below; read it once here (only the
    // feature-specific sub-blocks are guarded).
    auto customConfig = ::drogon::app().getCustomConfig();

    // Task 24 slice 5: WebAuthnService. rp_id/rp_name mirror
    // WebAuthnController.cc's own getRpId()/getRpName() config reads
    // (custom_config "webauthn" block, defaulting to "localhost"/"OAuth2
    // Server") -- read once here at startup instead of per-request, same
    // pattern as OAuth2Plugin.cc's own issuer/TTL config reads.
#ifdef WITH_WEBAUTHN
    auto webAuthnRepo =
      std::make_shared<authforge::storage::postgres::PostgresWebAuthnRepository>(dbClient);
    std::string rpId = "localhost";
    std::string rpName = "OAuth2 Server";
    if (customConfig.isMember("webauthn"))
    {
        rpId = customConfig["webauthn"].get("rp_id", rpId).asString();
        rpName = customConfig["webauthn"].get("rp_name", rpName).asString();
    }
#endif  // WITH_WEBAUTHN

    // Owned for the lifetime of the process (same static-local-in-a-
    // free-function pattern OAuth2Plugin.cc's static jwkManagerLogger
    // uses) -- SessionController/MfaController hold non-owning raw
    // pointers to these (see SessionController.h's/MfaController.h's
    // setIdentityAuthService()/setSessionManager()/setMfaService()/
    // setUserRepository() comments), and this function's only caller
    // (main.cc's registerBeginningAdvice) runs exactly once at startup.
    static auto authService =
      std::make_shared<authforge::identity::AuthService>(userRepo, crypto, clock);
    static auto notifier = std::make_shared<LoggingBackchannelLogoutNotifier>();
    static auto sessionManager = std::make_shared<authforge::identity::SessionManager>(notifier);
    static auto mfaService =
      std::make_shared<authforge::identity::MfaService>(mfaRepo, crypto, clock);
#ifdef WITH_WEBAUTHN
    static auto webAuthnService =
      std::make_shared<authforge::identity::WebAuthnService>(webAuthnRepo, crypto, rpId, rpName);
#endif  // WITH_WEBAUTHN

#ifdef WITH_SOCIAL
    // Task 24 slice 5: Social auth services (Google/WeChat/GitHub). Config
    // keys mirror each pre-Task-24 controller's own getXxxConfig() reads
    // (custom_config "external_auth.{google,wechat,github}" blocks) --
    // read once here at startup instead of per-request.
    std::string googleClientId, googleClientSecret, googleRedirectUri;
    std::string wechatAppId, wechatSecret;
    std::string githubClientId, githubClientSecret;
    if (customConfig.isMember("external_auth"))
    {
        const auto &externalAuth = customConfig["external_auth"];
        if (externalAuth.isMember("google"))
        {
            googleClientId = externalAuth["google"].get("client_id", "").asString();
            googleClientSecret = externalAuth["google"].get("client_secret", "").asString();
            googleRedirectUri = externalAuth["google"].get("redirect_uri", "").asString();
        }
        if (externalAuth.isMember("wechat"))
        {
            wechatAppId = externalAuth["wechat"].get("appid", "").asString();
            wechatSecret = externalAuth["wechat"].get("secret", "").asString();
        }
        if (externalAuth.isMember("github"))
        {
            githubClientId = externalAuth["github"].get("client_id", "").asString();
            githubClientSecret = externalAuth["github"].get("client_secret", "").asString();
        }
    }
    auto oauthHttpClient = std::make_shared<authforge::drogon::adapters::DrogonOAuthHttpClient>();
    auto socialAccountRepo =
      std::make_shared<authforge::storage::postgres::PostgresSocialAccountRepository>(dbClient);

    static auto googleAuthService = std::make_shared<authforge::identity::GoogleAuthService>(
      oauthHttpClient, googleClientId, googleClientSecret, googleRedirectUri
    );
    static auto weChatAuthService = std::make_shared<authforge::identity::WeChatAuthService>(
      oauthHttpClient, wechatAppId, wechatSecret
    );
    static auto gitHubAuthService = std::make_shared<authforge::identity::GitHubAuthService>(
      oauthHttpClient, socialAccountRepo, githubClientId, githubClientSecret
    );
#endif  // WITH_SOCIAL

    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::SessionController>()
      ->setIdentityAuthService(authService.get());
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::SessionController>()
      ->setSessionManager(sessionManager.get());
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::MfaController>()
      ->setMfaService(mfaService.get());
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::MfaController>()
      ->setUserRepository(userRepo.get());
#ifdef WITH_WEBAUTHN
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::WebAuthnController>()
      ->setWebAuthnService(webAuthnService.get());
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::WebAuthnController>()
      ->setUserRepository(userRepo.get());
#endif  // WITH_WEBAUTHN
#ifdef WITH_SOCIAL
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::GoogleController>()
      ->setGoogleAuthService(googleAuthService.get());
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::WeChatController>()
      ->setWeChatAuthService(weChatAuthService.get());
    drogon::DrClassMap::getSingleInstance<authforge::drogon::controllers::GitHubController>()
      ->setGitHubAuthService(gitHubAuthService.get());
#endif  // WITH_SOCIAL

    LOG_INFO << "Identity services wired into SessionController/MfaController/"
                "WebAuthnController/Google|WeChat|GitHubController "
                "(authforge::identity::AuthService + SessionManager + MfaService + "
                "WebAuthnService + Social auth services)";
}

}  // namespace bootstrap
