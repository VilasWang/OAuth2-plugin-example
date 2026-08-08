#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <authforge/drogon/filters/OAuth2AuthFilter.h>
#include <authforge/oauth2/jwk/JwkManager.h>
#include <authforge/drogon/adapters/DrogonLogger.h>
#include <authforge/drogon/adapters/StorageRoleProvider.h>
#include <authforge/drogon/adapters/OpenSslCryptoProvider.h>
#include <authforge/drogon/adapters/DrogonAuditSink.h>
#include <authforge/drogon/adapters/DrogonMetrics.h>
#include <authforge/drogon/adapters/StorageSubjectResolver.h>
#include <authforge/oauth2/protocol/AuthorizationService.h>
// Phase 4.6a: the god impls + bridges are gone; the plugin now constructs the
// per-backend RepositoryBundle and extracts its four oauth2 split-repository
// handles (Phase 1.5e: the bundle's 3 identity accessors are gone; identity
// repos are constructed separately from authforge::identity::* backing stores).
#include <authforge/storage/memory/MemoryRepositoryBundle.h>
#include <authforge/storage/postgres/PostgresRepositoryBundle.h>
#include <authforge/storage/redis/RedisRepositoryBundle.h>
#include <authforge/oauth2/repository/IClientRepository.h>
#include <authforge/oauth2/repository/IGrantRepository.h>
#include <authforge/oauth2/repository/ITokenRepository.h>
#include <authforge/oauth2/repository/IConsentRepository.h>
#include <authforge/oauth2/pkce/Pkce.h>
// authforge::identity::IdentityService (the thin forwarder over bundle repos) is still
// constructed here for the scopeRequiresAdminRole pure-function path.
#include <authforge/drogon/services/IdentityService.h>
// F-018: process-wide sliding-window rate limiter (token/introspect/revoke/
// device-polling). Header-only, framework-free; configured once at startup.
#include <authforge/common/utils/RateLimiter.h>
#include <drogon/drogon.h>

using namespace drogon;

void OAuth2Plugin::initAndStart(const Json::Value &config)
{
    LOG_INFO << "OAuth2Plugin loading...";

    // M3 Task 20 continuation (authforge-sdk-refactor): the explicit
    // OAuth2StandardController::initApiDocs() call that used to live here
    // was removed to break a circular dependency -- OAuth2StandardController
    // now lives in libs/drogon, which itself links OAuth2Plugin (for the
    // not-yet-relocated authforge::common::error machinery; see
    // libs/drogon/CMakeLists.txt). OAuth2Plugin therefore cannot #include
    // anything from libs/drogon without creating a link cycle.
    // initApiDocs() is still called explicitly and unconditionally by every
    // production/test entry point before drogon::app().run() (see
    // apps/server/main.cc and tests/test_main.cc), and is
    // idempotent (call_once guarded), so doc registration is unaffected by
    // this removal -- it just no longer ALSO happens from inside the plugin.

    initStorage(config);

    // Load TTL Config
    if (config.isMember("tokens"))
    {
        auto tokens = config["tokens"];
        authCodeTtl_ = tokens.get("auth_code_ttl", 600).asInt64();
        accessTokenTtl_ = tokens.get("access_token_ttl", 3600).asInt64();
        refreshTokenTtl_ = tokens.get("refresh_token_ttl", 2592000).asInt64();
    }

    // Initialize JWK Manager for OIDC id_token signing.
    // Defect 1.5 fix (init-once-then-read-only + immutable publish): build a
    // mutable JwkManager locally, run init() exactly once HERE — during
    // initAndStart(), before the server accepts requests / posts tasks to the
    // event loop — then publish it as a std::shared_ptr<const JwkManager>. The
    // const pointer makes the key state immutable at the type level for every
    // downstream holder (jwkManager_, tokenService_, the JWKS controller), so
    // run-time reads (signJwt/getJwks) cannot race any write. The "init before
    // request acceptance" ordering provides the happens-before edge (see
    // JwkManager.h); no per-read lock is needed.
    // M2b Task 17 slice 10: JwkManager now requires an explicit ILogger to
    // produce any log output (it no longer has a hardcoded Drogon-backed
    // fallback, since it moved into the Domain layer). Pass a
    // DrogonLogger explicitly here to preserve the pre-move log behavior.
    static authforge::drogon::adapters::DrogonLogger jwkManagerLogger;
    auto jwkManager = std::make_shared<authforge::oauth2::JwkManager>(&jwkManagerLogger);
    if (config.isMember("oidc"))
    {
        jwkManager->init(config["oidc"]);
    }
    else
    {
        // Initialize with empty config (will generate ephemeral key)
        Json::Value emptyConfig;
        jwkManager->init(emptyConfig);
    }
    // Publish as shared_ptr<const JwkManager>: read-only from here on.
    jwkManager_ = jwkManager;

    // M2b Task 17 slice 11: issuer is now a TokenService constructor
    // parameter (read once, here, at startup) rather than being read via
    // drogon::app().getCustomConfig() at id_token-issuance time -- same
    // config value, same default, just read at a different (equally
    // startup-time) point.
    std::string issuer = "http://localhost:5555";
    auto customConfig = drogon::app().getCustomConfig();
    if (customConfig.isMember("metadata") && customConfig["metadata"].isMember("issuer"))
    {
        issuer = customConfig["metadata"]["issuer"].asString();
    }
    // F-016: normalize a trailing slash once, at the single startup read, so
    // every consumer (token issuance, introspection backfill, discovery) sees
    // the same byte string and discovery endpoints never produce
    // "https://issuer//oauth2/...".
    while (issuer.size() > 1 && issuer.back() == '/')
        issuer.pop_back();
    issuer_ = issuer;
    // F-016: an http:// issuer is only sane for local development; anything
    // else means tokens/discovery will advertise a non-TLS issuer in a
    // deployment that is presumably reachable over the network.
    if (issuer.rfind("http://", 0) == 0 &&
        issuer.find("localhost") == std::string::npos &&
        issuer.find("127.0.0.1") == std::string::npos &&
        issuer.find("[::1]") == std::string::npos)
    {
        LOG_WARN << "OAuth2Plugin: metadata.issuer \"" << issuer
                 << "\" uses plain http on a non-loopback host; production "
                    "deployments MUST configure an https:// issuer";
    }

    // F-018: configure the process-wide failure rate limiter for the token
    // / introspect / revoke / device-code-polling endpoints. Reads
    // custom_config["auth"]["rate_limit"] once at startup; if absent the
    // built-in defaults stand (30 failures per (ip+client_id) per 60s). The
    // limiter is a function-local singleton, so this single configure() call
    // is seen by all four controllers that consult it.
    {
        authforge::common::utils::RateLimiterConfig rlCfg =
          authforge::common::utils::RateLimiterConfig::defaults();
        if (customConfig.isMember("auth") &&
            customConfig["auth"].isMember("rate_limit") &&
            customConfig["auth"]["rate_limit"].isObject())
        {
            const auto &rl = customConfig["auth"]["rate_limit"];
            if (rl.isMember("max_failures") && rl["max_failures"].isUInt())
                rlCfg.maxFailures =
                  static_cast<std::size_t>(rl["max_failures"].asUInt());
            if (rl.isMember("window_seconds") && rl["window_seconds"].isUInt())
                rlCfg.windowSeconds =
                  std::chrono::seconds(rl["window_seconds"].asUInt());
        }
        authforge::common::utils::RateLimiter::instance().configure(rlCfg);
        LOG_DEBUG << "OAuth2Plugin: rate limiter configured (max_failures="
                  << rlCfg.maxFailures
                  << ", window_seconds=" << rlCfg.windowSeconds.count() << ")";
    }

    // Initialize Services
    // M3 Task 24 slice 2 (authforge-sdk-refactor): tokenService_/
    // clientService_ are now the NEW Domain-layer classes
    // (authforge::oauth2::protocol::TokenService/ClientService, Task 17),
    // constructed against the split repository interfaces via
    // LegacyStorageRepositoryBridge (Task 24 slice 1) -- storage_ itself
    // is untouched (still the old oauth2::IOAuth2Storage concrete
    // classes), so this is purely a construction-site change, not a
    // storage migration. Every controller keeps calling
    // plugin->exchangeCodeForToken(...) etc unchanged (OAuth2Plugin was
    // already a clean forwarding facade); only the delegate methods'
    // bodies below (and this construction) know about the new types.
    // Phase 4.6a: clientRepo_/grantRepo_/tokenRepo_/consentRepo_/roleRepo_/
    // userRepo_/subjectMappingRepo_ are now populated by initStorage() directly
    // from the per-backend RepositoryBundle (no bridges, no storage_). The
    // services below consume these handles.
    auto cryptoProvider = std::make_shared<authforge::drogon::adapters::OpenSslCryptoProvider>();
    auto auditSink = std::make_shared<authforge::drogon::adapters::DrogonAuditSink>();
    // M8 Task 40 decision b: publish the Adapter-side observability ports as
    // plugin members so Drogon-layer controllers can emit audit/metrics via
    // authforge::common::ports::* (getAuditSink()/getMetrics()) instead of
    // AuditLogger/Metrics statics.
    auditSink_ = auditSink;
    metrics_ = std::make_shared<authforge::drogon::adapters::DrogonMetrics>();

    // Phase 4.5: roles resolve through StorageRoleProvider's subject-string
    // overload (supportsSubjectLookup()=true) -- byte-equivalent to the legacy
    // single-hop storage_->getUserRoles(subjectString), and retires the
    // LegacyRoleResolutionBridge's synthetic-id/pending-roles shim. The new
    // IRoleProvider port carries the string overload; oauth2::protocol::
    // TokenService prefers it, so no ISubjectResolver is needed (passed null).
    // Phase 4.6a: now backed by roleRepo_ (the identity split-repo), not storage_.
    roleProvider_ = std::make_shared<authforge::drogon::adapters::StorageRoleProvider>(roleRepo_);

    // B10 / Task 45: wire the Domain-layer AuthorizationService (the protocol
    // engine) so /oauth2/authorize can call evaluateScopes() instead of the
    // controller's inline 3-tier chain. The engine needs an ISubjectResolver
    // (to turn "provider:localId" into internalUserId for the consent + role
    // tiers) -- StorageSubjectResolver backs it with subjectMappingRepo_, the
    // same repo IdentityService::getInternalUserId used in the old inline path.
    subjectResolver_ =
      std::make_shared<authforge::drogon::adapters::StorageSubjectResolver>(subjectMappingRepo_);
    authorizationService_ = std::make_shared<authforge::oauth2::protocol::AuthorizationService>(
      clientRepo_, consentRepo_, subjectResolver_, roleProvider_
    );

    // Defect 1.3 fix: services now share ownership of storage_ (shared_ptr),
    // so the storage lifetime is guaranteed to cover every service instead of
    // relying on the implicit "storage_ outlives services" timing convention.
    // The new TokenService continues that guarantee transitively via the
    // repository bridges above, which each hold their own storage_ shared_ptr.
    tokenService_ = std::make_shared<authforge::oauth2::protocol::TokenService>(
      clientRepo_,
      grantRepo_,
      tokenRepo_,
      cryptoProvider,
      auditSink,
      nullptr,        // ISubjectResolver -- not needed; roleProvider_ resolves
                      // roles by subject string directly (phase 4.5).
      roleProvider_,  // IRoleProvider (subject-string path)
      authCodeTtl_,
      accessTokenTtl_,
      refreshTokenTtl_,
      issuer
    );
    tokenService_->setJwkManager(jwkManager_);
    clientService_ = std::make_shared<authforge::oauth2::protocol::ClientService>(clientRepo_);
    identityService_ = std::make_shared<authforge::identity::IdentityService>(
      authforge::identity::IdentityService::Repos{
        roleRepo_, userRepo_, subjectMappingRepo_, consentRepo_
      }
    );

    // Initialize Cleanup Service (Phase 4.2: now keyed on the NEW split repos,
    // not storage_. grantRepo is the local bridge over storage_; tokenRepo_ is
    // the member retained in 4.1.)
    cleanupService_ =
      std::make_shared<authforge::drogon::OAuth2CleanupService>(grantRepo_, tokenRepo_);
    double cleanupInterval = config.get("cleanup_interval_seconds", 3600.0).asDouble();
    cleanupService_->start(cleanupInterval);

    LOG_INFO << "OAuth2Plugin initialized with storage type: " << storageType_;
}

void OAuth2Plugin::initStorage(const Json::Value &config)
{
    // Phase 1.5d (Task 39): storage_ (the god IOAuth2Storage) is gone. The
    // plugin constructs the per-backend RepositoryBundle (Memory/Postgres/
    // Redis) for the 4 oauth2 repos and extracts those handles into members;
    // the 3 identity repos are constructed SEPARATELY from the NEW
    // authforge::identity::* backing stores (PostgresIdentityRepository /
    // MemoryIdentityRepository), which multiply-inherit all 3 interfaces so a
    // single shared instance backs roleRepo_/userRepo_/subjectMappingRepo_.
    // The bundle's own 3 identity accessors are intentionally NOT called
    // here (they return the legacy oauth2::* types; 1.5e deletes them).
    storageType_ = config.get("storage_type", "memory").asString();

    // Helper: assign the 4 oauth2 repos from a bundle + the 3 identity repos
    // from a single identity-repo shared_ptr.
    auto assignOAuth2 = [this](
                          std::shared_ptr<authforge::oauth2::repository::IClientRepository> c,
                          std::shared_ptr<authforge::oauth2::repository::IGrantRepository> g,
                          std::shared_ptr<authforge::oauth2::repository::ITokenRepository> t,
                          std::shared_ptr<authforge::oauth2::repository::IConsentRepository> cn
                        ) {
        clientRepo_ = std::move(c);
        grantRepo_ = std::move(g);
        tokenRepo_ = std::move(t);
        consentRepo_ = std::move(cn);
    };

    if (storageType_ == "postgres")
    {
        authforge::storage::postgres::PostgresRepositoryBundle bundle;
        bundle.initFromConfig(config["postgres"]);
        assignOAuth2(
          bundle.clientRepository(),
          bundle.grantRepository(),
          bundle.tokenRepository(),
          bundle.consentRepository()
        );

        // Resolve the same DbClientPtr the bundle's PostgresRepositoryBase
        // uses (db_client_name from the postgres config block, default
        // "default"), then construct the NEW identity backing store. Mirrors
        // OAuth2Server/bootstrap/IdentityAssembly.cc's construction.
        std::string dbClientName = config["postgres"].get("db_client_name", "default").asString();
        auto dbClient = drogon::app().getDbClient(dbClientName);
        auto identityRepo =
          std::make_shared<authforge::storage::postgres::PostgresIdentityRepository>(dbClient);
        // One instance backs all 3 identity interfaces (multiple inheritance).
        roleRepo_ = identityRepo;
        userRepo_ = identityRepo;
        subjectMappingRepo_ = identityRepo;

        LOG_INFO << "Using PostgreSQL storage backend (RepositoryBundle)";
    }
    else if (storageType_ == "redis")
    {
        // F-005: standalone Redis storage is DEPRECATED. Its refresh-token
        // persistence was always a no-op (rotation/reuse-cascade silently
        // non-functional); the supported production topology is postgres
        // storage + (future) redis cache layer. The mode still boots for
        // backward compatibility, but refresh_token grant is rejected
        // explicitly (see refreshAccessToken) instead of failing with a
        // misleading invalid_grant.
        LOG_ERROR << "OAuth2Plugin: storage_type=\"redis\" is DEPRECATED and "
                     "will be removed. Refresh-token rotation is NOT "
                     "functional in this mode (refresh_token grant is "
                     "rejected). Use storage_type=\"postgres\"; Redis will "
                     "return only as a cache layer in front of Postgres.";
        std::string clientName = config["redis"].get("client_name", "default").asString();
        authforge::storage::redis::RedisRepositoryBundle bundle(clientName);
        assignOAuth2(
          bundle.clientRepository(),
          bundle.grantRepository(),
          bundle.tokenRepository(),
          bundle.consentRepository()
        );

        // Phase 1.5c decision: there is NO new Redis identity impl (the
        // legacy Redis identity repos were always placeholders that returned
        // nullopt / {"user"}). Fall back to MemoryIdentityRepository so the
        // identity path stays functional without a real Redis user store.
        LOG_WARN << "OAuth2Plugin: redis storage_type has no dedicated "
                    "identity backend; using MemoryIdentityRepository as a "
                    "placeholder (role lookups default to {\"user\"}, no "
                    "real user store)";
        auto identityRepo =
          std::make_shared<authforge::storage::memory::MemoryIdentityRepository>();
        // Unconditional call (Phase 7 regression fix): the legacy path always
        // passed config["admin_users"] (null when absent), and
        // initAdminRoles' no-config branch injects the default
        // admin -> {admin, user} mapping. Gating on isMember() made that
        // branch unreachable and dropped the default admin roles.
        identityRepo->initAdminRoles(config["admin_users"]);
        roleRepo_ = identityRepo;
        userRepo_ = identityRepo;
        subjectMappingRepo_ = identityRepo;
    }
    else
    {
        authforge::storage::memory::MemoryRepositoryBundle bundle;
        if (config.isMember("clients"))
            bundle.initFromConfig(config["clients"]);
        assignOAuth2(
          bundle.clientRepository(),
          bundle.grantRepository(),
          bundle.tokenRepository(),
          bundle.consentRepository()
        );

        // Memory backend: NEW identity backing store, with the admin role map
        // populated from the same "admin_users" config block the legacy
        // MemoryRoleRepository::initFromConfig consumed.
        auto identityRepo =
          std::make_shared<authforge::storage::memory::MemoryIdentityRepository>();
        // Unconditional: see the redis-branch note above (missing key -> null
        // -> initAdminRoles injects the legacy default admin mapping).
        identityRepo->initAdminRoles(config["admin_users"]);
        roleRepo_ = identityRepo;
        userRepo_ = identityRepo;
        subjectMappingRepo_ = identityRepo;
    }
}

void OAuth2Plugin::shutdown()
{
    // Explicit destruction order (defect 1.3 fix): stop the cleanup timer
    // first so no new cleanup callback is dispatched, then release the service
    // objects, and finally drop the repository handles. Because the services
    // share ownership of the repos (shared_ptr), releasing them before resetting
    // the repo handles guarantees the underlying storage outlives every
    // service. Any in-flight async callback that captured a repo shared_ptr
    // keeps the relevant object alive until it completes, so the storage is
    // destroyed only after the last user is gone.
    //
    // Defect 1.8 (self-capture interaction): the split-repository classes
    // capture `auto self = shared_from_this();` in their async continuations, so
    // a reset() here may only drop OUR reference; if a Redis/DB callback is
    // still in flight, the last reference is held by that continuation and the
    // repo's destructor runs on the redis/DB client's loop thread once the
    // callback completes. The shared-ownership guarantee makes that deferred
    // destruction safe. We intentionally do not add extra synchronization here:
    // the strong `self` reference is the lifetime contract.
    if (cleanupService_)
        cleanupService_->stop();

    cleanupService_.reset();
    tokenService_.reset();
    clientService_.reset();
    identityService_.reset();
    roleProvider_.reset();

    // Drop the repository handles (releases the bundle's underlying state once
    // the last in-flight callback completes).
    clientRepo_.reset();
    grantRepo_.reset();
    tokenRepo_.reset();
    consentRepo_.reset();
    roleRepo_.reset();
    userRepo_.reset();
    subjectMappingRepo_.reset();
}

// ========== Delegate to Services ==========

void OAuth2Plugin::validateClient(
  const std::string &clientId,
  const std::string &clientSecret,
  std::function<void(bool)> &&callback
)
{
    clientService_->validateClient(clientId, clientSecret, std::move(callback));
}

void OAuth2Plugin::validateRedirectUri(
  const std::string &clientId,
  const std::string &redirectUri,
  std::function<void(bool)> &&callback
)
{
    clientService_->validateRedirectUri(clientId, redirectUri, std::move(callback));
}

void OAuth2Plugin::generateAuthorizationCode(
  const std::string &clientId,
  const std::string &subject,
  const std::string &scope,
  const std::string &redirectUri,
  const std::string &codeChallenge,
  const std::string &codeChallengeMethod,
  const std::string &nonce,
  std::function<void(bool, std::string, std::string)> &&callback,
  int64_t authTime,
  const std::string &amr
)
{
    tokenService_->generateAuthorizationCode(
      clientId,
      subject,
      scope,
      redirectUri,
      codeChallenge,
      codeChallengeMethod,
      nonce,
      std::move(callback),
      authTime,
      amr
    );
}

void OAuth2Plugin::exchangeCodeForToken(
  const std::string &code,
  const std::string &clientId,
  const std::string &clientSecret,
  const std::string &redirectUri,
  const std::string &codeVerifier,
  std::function<void(const Json::Value &)> &&callback
)
{
    tokenService_->exchangeCodeForToken(
      code, clientId, clientSecret, redirectUri, codeVerifier, std::move(callback)
    );
}

void OAuth2Plugin::refreshAccessToken(
  const std::string &refreshToken,
  const std::string &clientId,
  std::function<void(const Json::Value &)> &&callback
)
{
    // F-005: the standalone Redis backend never persisted refresh tokens
    // (saveRefreshToken/getRefreshToken are no-ops), so rotation and
    // reuse-detection silently do not work there. That mode is deprecated
    // (postgres storage + redis cache is the target architecture); reject
    // the grant explicitly instead of surfacing a misleading invalid_grant.
    if (storageType_ == "redis")
    {
        Json::Value err;
        err["error"] = "unsupported_grant_type";
        err["error_description"] =
          "refresh_token grant is not supported with storage_type=\"redis\" "
          "(deprecated mode without refresh-token persistence); use "
          "storage_type=\"postgres\"";
        if (callback)
            callback(err);
        return;
    }
    tokenService_->refreshAccessToken(refreshToken, clientId, std::move(callback));
}

void OAuth2Plugin::validateAccessToken(
  const std::string &token,
  std::function<void(std::shared_ptr<AccessToken>)> &&callback
)
{
    // M3 Task 24 slice 2: new TokenService returns
    // shared_ptr<authforge::oauth2::model::OAuth2AccessToken>; convert to
    // the old oauth2::OAuth2AccessToken (== AccessToken alias) at this
    // boundary so every controller call site (which expects the OLD type)
    // is unaffected.
    tokenService_->validateAccessToken(
      token,
      [callback =
         std::move(callback)](std::shared_ptr<authforge::oauth2::model::OAuth2AccessToken> t) {
          if (!t)
          {
              callback(nullptr);
              return;
          }
          callback(std::make_shared<AccessToken>(*t));
      }
    );
}

void OAuth2Plugin::getUserRoles(
  const std::string &userId,
  std::function<void(std::vector<std::string>)> &&callback
)
{
    identityService_->getUserRoles(userId, std::move(callback));
}

void OAuth2Plugin::getInternalUserId(
  const std::string &subject,
  std::function<void(std::optional<int32_t>)> &&callback
)
{
    identityService_->getInternalUserId(subject, std::move(callback));
}

void OAuth2Plugin::hasUserConsent(
  int32_t internalUserId,
  const std::string &clientId,
  const std::string &scope,
  std::function<void(bool)> &&callback
)
{
    identityService_->hasUserConsent(internalUserId, clientId, scope, std::move(callback));
}

void OAuth2Plugin::saveUserConsent(
  int32_t internalUserId,
  const std::string &clientId,
  const std::string &scope,
  std::function<void(bool)> &&callback
)
{
    identityService_->saveUserConsent(internalUserId, clientId, scope, std::move(callback));
}

void OAuth2Plugin::validateClientScopes(
  const std::string &clientId,
  const std::vector<std::string> &requestedScopes,
  std::function<void(bool, std::string)> &&callback
)
{
    clientService_->validateClientScopes(clientId, requestedScopes, std::move(callback));
}

void OAuth2Plugin::validateUserRolesForScopes(
  const std::string &userId,
  const std::vector<std::string> &scopes,
  std::function<void(bool, std::string)> &&callback
)
{
    identityService_->validateUserRolesForScopes(userId, scopes, std::move(callback));
}

void OAuth2Plugin::introspectToken(
  const std::string &token,
  std::function<void(std::optional<authforge::oauth2::model::TokenIntrospection>)> &&callback
)
{
    // A3: pass the NEW authforge::oauth2::model::TokenIntrospection straight
    // through (no legacy-DTO conversion -- IOAuth2Storage.h is being deleted).
    tokenService_->introspectToken(token, std::move(callback));
}

void OAuth2Plugin::incrementIntrospectCount(
  const std::string &token,
  std::function<void()> &&callback
)
{
    // Phase 4.1: route through the NEW ITokenRepository (today the bridge over
    // storage_; later the direct split-repo) instead of storage_ directly --
    // removes the last direct god-facade call inside the plugin besides the
    // legacy service constructors.
    if (tokenRepo_)
        tokenRepo_->incrementIntrospectCount(token, std::move(callback));
}

void OAuth2Plugin::getClient(
  const std::string &clientId,
  authforge::oauth2::repository::IClientRepository::ClientCallback &&callback
)
{
    // Phase 4.3: forward through the NEW IClientRepository so controllers no
    // longer reach into storage_ via getStorage().
    if (clientRepo_)
        clientRepo_->getClient(clientId, std::move(callback));
}

void OAuth2Plugin::saveAccessToken(
  const authforge::oauth2::model::OAuth2AccessToken &token,
  std::function<void()> &&callback
)
{
    if (tokenRepo_)
        tokenRepo_->saveAccessToken(token, std::move(callback));
}

void OAuth2Plugin::saveTokenPair(
  const authforge::oauth2::model::OAuth2AccessToken &accessToken,
  const authforge::oauth2::model::OAuth2RefreshToken &refreshToken,
  std::function<void(bool ok)> &&callback
)
{
    if (tokenRepo_)
    {
        tokenRepo_->saveTokenPair(accessToken, refreshToken, std::move(callback));
        return;
    }
    // No token repository configured: report failure instead of dropping
    // the callback (dropping it leaves the HTTP request hanging until
    // client timeout).
    LOG_ERROR << "saveTokenPair: no token repository configured";
    if (callback)
        callback(false);
}

void OAuth2Plugin::getUserInfo(
  const std::string &userId,
  std::function<void(std::optional<Json::Value>)> &&callback
)
{
    // Phase 1.5d (Task 39): routed through userRepo_ (the NEW
    // authforge::identity::IUserRepository). That interface has no
    // getUserInfo(string) overload -- it exposes findById(int32) /
    // findByPublicSub(string) returning UserData. Replicate the legacy
    // dispatch (numeric -> findById(stoi); otherwise -> findByPublicSub) and
    // rebuild the legacy JSON shape ({id, username, email}) the caller
    // (TokenEndpointController's userinfo endpoint) consumes byte-for-byte.
    if (!userRepo_)
    {
        callback(std::nullopt);
        return;
    }

    // Numeric userId -> internal int32 id; otherwise treat as public_sub.
    bool isNumeric = false;
    int32_t numericId = 0;
    try
    {
        size_t pos = 0;
        int parsed = std::stoi(userId, &pos);
        isNumeric = (pos == userId.length());
        if (isNumeric)
            numericId = parsed;
    }
    catch (...)
    {
        isNumeric = false;
    }

    auto buildJson =
      [callback = std::move(callback)](std::optional<authforge::identity::UserData> data) mutable {
          if (!data)
          {
              callback(std::nullopt);
              return;
          }
          Json::Value userInfo;
          userInfo["id"] = data->id;
          userInfo["username"] = data->username;
          userInfo["email"] = data->email;
          // F-024 (OIDC Core §5.1): expose email_verified so RPs can tell
          // verified from unverified email addresses. UserData carries this
          // from the users row (Task 39 widened the identity repository).
          userInfo["email_verified"] = data->emailVerified;
          callback(userInfo);
      };

    if (isNumeric)
        userRepo_->findById(numericId, std::move(buildJson));
    else
        userRepo_->findByPublicSub(userId, std::move(buildJson));
}

void OAuth2Plugin::revokeAccessToken(
  const std::string &token,
  const std::string &revokedBy,
  std::function<void()> &&callback
)
{
    tokenService_->revokeAccessToken(token, revokedBy, std::move(callback));
}

bool OAuth2Plugin::validatePkceCodeVerifier(
  const std::string &codeVerifier,
  const std::string &codeChallenge,
  const std::string &codeChallengeMethod
)
{
    // A1: relocated off the legacy oauth2::TokenService static. Pure RFC 7636
    // §4.6 verification: plain = direct compare, S256 = base64url(SHA256).
    std::string method = codeChallengeMethod.empty() ? "plain" : codeChallengeMethod;
    if (method == "plain")
    {
        return codeVerifier == codeChallenge;
    }
    if (method == "S256")
    {
        return generateSha256Hash(codeVerifier) == codeChallenge;
    }
    return false;
}

std::string OAuth2Plugin::generateSha256Hash(const std::string &input)
{
    // A1: relocated off the legacy oauth2::TokenService static. Delegates to
    // the byte-identical, RFC 7636 Appendix-B-tested algorithm in the
    // authforge::oauth2::pkce Domain package.
    static authforge::drogon::adapters::OpenSslCryptoProvider cryptoProvider;
    return authforge::oauth2::pkce::computeCodeChallenge(input, "S256", cryptoProvider);
}

std::string OAuth2Plugin::signIdToken(
  const std::string &subject,
  const std::string &clientId,
  int64_t authTime,
  const std::string &amr
) const
{
    // F-025 (OIDC Core §12): refresh_token and device_code grants issue
    // tokens outside TokenService, so they cannot reuse its inline id_token
    // signing. This helper centralizes the claim set + signing so both
    // paths (and any future one) build identical id_tokens. Returns "" when
    // the JwkManager is not initialized, which callers map to "omit
    // id_token" (matching the authorization_code path in
    // TokenService::exchangeCodeForToken).
    if (!jwkManager_ || !jwkManager_->isInitialized())
        return "";

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch()
    )
                 .count();
    Json::Value claims;
    claims["iss"] = issuer_;
    claims["sub"] = subject;
    claims["aud"] = clientId;
    claims["iat"] = static_cast<Json::Int64>(now);
    // OIDC Core §2: id_token exp follows the access-token TTL (the longest
    // the access token remains usable, so the id_token stays valid for the
    // same window).
    claims["exp"] = static_cast<Json::Int64>(now + accessTokenTtl_);
    // F-022: carry auth_time/amr when available (refresh tokens issued from
    // an MFA-elevated login keep acr=2). On refresh/device these are often
    // absent (the refresh DTO does not persist them), in which case they are
    // omitted -- acceptable per OIDC §12 (auth_time is only required when
    // the original auth request had max_age).
    if (authTime > 0)
        claims["auth_time"] = static_cast<Json::Int64>(authTime);
    if (!amr.empty())
    {
        Json::Value amrArray(Json::arrayValue);
        size_t s = 0;
        while (s < amr.size())
        {
            size_t e = amr.find(' ', s);
            if (e == std::string::npos)
                e = amr.size();
            if (e > s)
                amrArray.append(amr.substr(s, e - s));
            s = e + 1;
        }
        if (!amrArray.empty())
        {
            claims["amr"] = amrArray;
            bool mfa = false;
            for (const auto &v : amrArray)
            {
                if (v.asString() == "mfa")
                {
                    mfa = true;
                    break;
                }
            }
            claims["acr"] = Json::Int64(mfa ? 2 : 1);
        }
    }
    return jwkManager_->signJwt(claims);
}

bool OAuth2Plugin::scopeRequiresAdminRole(const std::string &scope)
{
    return authforge::identity::IdentityService({}).scopeRequiresAdminRole(scope);
}

void OAuth2Plugin::ensureSubjectMapping(
  const std::string &subject,
  const std::string &username,
  int32_t internalUserId,
  std::function<void()> &&callback
)
{
    identityService_->ensureSubjectMapping(subject, username, internalUserId, std::move(callback));
}

void OAuth2Plugin::handleFirstTimeLogin(
  const std::string &subject,
  const std::string &provider,
  std::function<void(int32_t)> &&callback
)
{
    identityService_->handleFirstTimeLogin(subject, provider, std::move(callback));
}
