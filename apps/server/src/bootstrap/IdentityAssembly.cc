#include "IdentityAssembly.h"

#include <fulla/drogon/adapters/BackchannelLogoutNotifier.h>
#include <fulla/drogon/adapters/DrogonOAuthHttpClient.h>
#ifdef WITH_SOCIAL
#include <fulla/drogon/controllers/GitHubController.h>
#include <fulla/drogon/controllers/GoogleController.h>
#include <fulla/drogon/controllers/WeChatController.h>
#endif  // WITH_SOCIAL
#include <fulla/drogon/controllers/MfaController.h>
#include <fulla/drogon/controllers/SessionController.h>
#include <fulla/drogon/controllers/UserSelfServiceController.h>
#ifdef WITH_WEBAUTHN
#include <fulla/drogon/controllers/WebAuthnController.h>
#endif  // WITH_WEBAUTHN
#include <fulla/identity/AuthService.h>
#include <fulla/identity/IMfaRepository.h>
#include <fulla/identity/IWebAuthnRepository.h>
#include <fulla/identity/MfaService.h>
#include <fulla/identity/SessionManager.h>
#include <fulla/identity/SocialAuthService.h>
#include <fulla/identity/SocialLinkService.h>
#include <fulla/identity/WebAuthnService.h>
#include <fulla/drogon/adapters/RedisSocialLinkStateStore.h>
#include <fulla/storage/postgres/PostgresIdentityRepository.h>
#include <fulla/storage/postgres/PostgresMfaRepository.h>
#include <fulla/storage/postgres/PostgresSocialAccountRepository.h>
#include <fulla/storage/postgres/PostgresWebAuthnRepository.h>
#include <drogon/DrClassMap.h>
#include <drogon/drogon.h>
#include <fulla/drogon/adapters/OpenSslCryptoProvider.h>
#include <fulla/drogon/adapters/SystemClock.h>
#include <fulla/drogon/plugin/OAuth2Plugin.h>

#include <memory>

namespace bootstrap
{

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
                    "fulla::drogon::services::AuthService path";
        return;
    }

    auto userRepo =
      std::make_shared<fulla::storage::postgres::PostgresIdentityRepository>(dbClient);
    auto crypto = std::make_shared<fulla::drogon::adapters::OpenSslCryptoProvider>();
    auto clock = std::make_shared<fulla::drogon::adapters::SystemClock>();

    // Task 24 slice 5: MfaService, sharing the same IUserRepository/
    // ICryptoProvider/IClock instances constructed above (MfaService only
    // needs its own IMfaRepository -- a distinct interface/table-column
    // scope, see IMfaRepository.h's header comment -- not a new crypto/
    // clock).
    auto mfaRepo = std::make_shared<fulla::storage::postgres::PostgresMfaRepository>(dbClient);

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
      std::make_shared<fulla::storage::postgres::PostgresWebAuthnRepository>(dbClient);
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
      std::make_shared<fulla::identity::AuthService>(userRepo, crypto, clock);
    // B1: shared outbound-HTTP client (Drogon-backed) used by both the
    // backchannel-logout notifier and, when built, the social auth services.
    static auto oauthHttpClient =
      std::make_shared<fulla::drogon::adapters::DrogonOAuthHttpClient>();
    // B1 (OIDC Back-Channel Logout 1.0): real notifier -- finds each relying
    // party with an active session + a registered backchannel_logout_uri and
    // POSTs a signed logout_token, fire-and-forget. If the plugin's signing
    // key / audit sink is unavailable, notify() degrades to a no-op fan-out
    // (the logout flow is never blocked). plugin is initialized by the time
    // this runs (getStorageType() above already relied on initAndStart()).
    static auto notifier =
      std::make_shared<fulla::drogon::adapters::BackchannelLogoutNotifier>(
        dbClient,
        plugin ? plugin->getJwkManager() : nullptr,
        plugin ? plugin->getIssuer() : std::string{},
        oauthHttpClient,
        plugin ? plugin->getAuditSink() : nullptr);
    static auto sessionManager = std::make_shared<fulla::identity::SessionManager>(notifier);
    static auto mfaService =
      std::make_shared<fulla::identity::MfaService>(mfaRepo, crypto, clock);
#ifdef WITH_WEBAUTHN
    static auto webAuthnService =
      std::make_shared<fulla::identity::WebAuthnService>(webAuthnRepo, crypto, rpId, rpName);
#endif  // WITH_WEBAUTHN

#ifdef WITH_SOCIAL
    // Task 24 slice 5: Social auth services (Google/WeChat/GitHub). Config
    // keys mirror each pre-Task-24 controller's own getXxxConfig() reads
    // (custom_config "external_auth.{google,wechat,github}" blocks) --
    // read once here at startup instead of per-request.
    // #111: a provider whose required credentials are absent or still carry
    // the legacy YOUR_* template placeholders is DISABLED at startup (its
    // service is not injected): misconfigured providers fail at request time
    // with a clear NotConfigured error instead of phoning an upstream that
    // can never accept them.
    const auto credentialConfigured = [](const std::string &value) {
        return !value.empty() && value.rfind("YOUR_", 0) != 0;
    };
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
    const bool googleConfigured =
      credentialConfigured(googleClientId) && credentialConfigured(googleClientSecret);
    const bool wechatConfigured =
      credentialConfigured(wechatAppId) && credentialConfigured(wechatSecret);
    const bool githubConfigured =
      credentialConfigured(githubClientId) && credentialConfigured(githubClientSecret);
    if (!googleConfigured)
        LOG_INFO << "IdentityAssembly: social provider 'google' disabled (external_auth.google "
                    "credentials not configured; see docs on provider setup)";
    if (!wechatConfigured)
        LOG_INFO << "IdentityAssembly: social provider 'wechat' disabled (external_auth.wechat "
                    "credentials not configured; see docs on provider setup)";
    if (!githubConfigured)
        LOG_INFO << "IdentityAssembly: social provider 'github' disabled (external_auth.github "
                    "credentials not configured; see docs on provider setup)";
    auto socialAccountRepo =
      std::make_shared<fulla::storage::postgres::PostgresSocialAccountRepository>(dbClient);

    static auto googleAuthService = std::make_shared<fulla::identity::GoogleAuthService>(
      oauthHttpClient, googleClientId, googleClientSecret, googleRedirectUri
    );
    static auto weChatAuthService = std::make_shared<fulla::identity::WeChatAuthService>(
      oauthHttpClient, wechatAppId, wechatSecret
    );
    static auto gitHubAuthService = std::make_shared<fulla::identity::GitHubAuthService>(
      oauthHttpClient, socialAccountRepo, githubClientId, githubClientSecret
    );
    // B2 social link/unlink: the same provider services + mapping repository
    // back the self-service /api/me/social/links* routes. Process-lifetime
    // static (same contract as the services above) so the raw pointer handed
    // to the controller singleton stays valid.
    // #73b: the last-credential guard also counts WebAuthn credentials when
    // built with WebAuthn support (passkey-only users may unlink their last
    // social link). cryptoProvider is wired for the server-side link-state
    // flow (#71).
#if defined(WITH_WEBAUTHN)
    std::shared_ptr<fulla::identity::IWebAuthnRepository> linkGuardWebAuthnRepo = webAuthnRepo;
#else
    std::shared_ptr<fulla::identity::IWebAuthnRepository> linkGuardWebAuthnRepo = nullptr;
#endif
    // #71: Redis-backed one-time link state (fail-closed when Redis is not
    // configured -- linking then answers NotConfigured rather than running
    // stateless, which is the exact surface this flow closes).
    std::shared_ptr<fulla::identity::ISocialLinkStateStore> linkStateStore = nullptr;
    try
    {
        auto redisClient = ::drogon::app().getRedisClient("default");
        linkStateStore =
          std::make_shared<fulla::drogon::adapters::RedisSocialLinkStateStore>(redisClient, crypto);
    }
    catch (const std::exception &)
    {
        LOG_WARN << "IdentityAssembly: no Redis client configured -- social link "
                    "authorization disabled (link endpoints fail closed, #71)";
    }
    static auto socialLinkService = std::make_shared<fulla::identity::SocialLinkService>(
      // #111: disabled providers enter as nullptr -> NotConfigured at the
      // link endpoints (same error surface as the login controllers).
      githubConfigured ? gitHubAuthService : nullptr,
      googleConfigured ? googleAuthService : nullptr,
      wechatConfigured ? weChatAuthService : nullptr,
      socialAccountRepo,
      linkGuardWebAuthnRepo,
      crypto,
      linkStateStore
    );
#endif  // WITH_SOCIAL

    drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::SessionController>()
      ->setIdentityAuthService(authService.get());
    drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::SessionController>()
      ->setSessionManager(sessionManager.get());
    drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::MfaController>()
      ->setMfaService(mfaService.get());
    drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::MfaController>()
      ->setUserRepository(userRepo.get());
#ifdef WITH_WEBAUTHN
    drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::WebAuthnController>()
      ->setWebAuthnService(webAuthnService.get());
    drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::WebAuthnController>()
      ->setUserRepository(userRepo.get());
#endif  // WITH_WEBAUTHN
#ifdef WITH_SOCIAL
    // #111: inject nullptr for disabled providers (envelope NotConfigured at
    // request time instead of a doomed upstream call).
    drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::GoogleController>()
      ->setGoogleAuthService(googleConfigured ? googleAuthService.get() : nullptr);
    drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::WeChatController>()
      ->setWeChatAuthService(wechatConfigured ? weChatAuthService.get() : nullptr);
    drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::GitHubController>()
      ->setGitHubAuthService(githubConfigured ? gitHubAuthService.get() : nullptr);
    drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::UserSelfServiceController>()
      ->setSocialLinkService(socialLinkService.get());
#endif  // WITH_SOCIAL

    LOG_INFO << "Identity services wired into SessionController/MfaController/"
                "WebAuthnController/Google|WeChat|GitHubController "
                "(fulla::identity::AuthService + SessionManager + MfaService + "
                "WebAuthnService + Social auth services)";
}

}  // namespace bootstrap
