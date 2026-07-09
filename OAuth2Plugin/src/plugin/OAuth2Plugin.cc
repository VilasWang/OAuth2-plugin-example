#include <oauth2/plugin/OAuth2Plugin.h>
#include <oauth2/filters/OAuth2AuthFilter.h>
#include <oauth2/utils/JwkManager.h>
#include <oauth2/adapters/DrogonLogger.h>
#include <oauth2/adapters/StorageRoleProvider.h>
#include <oauth2/storage/MemoryOAuth2Storage.h>
#include <oauth2/storage/PostgresOAuth2Storage.h>
#include <oauth2/storage/RedisOAuth2Storage.h>
#include <oauth2/storage/CachedOAuth2Storage.h>
#include <drogon/drogon.h>

using namespace drogon;

void OAuth2Plugin::initAndStart(const Json::Value &config)
{
    LOG_INFO << "OAuth2Plugin loading...";

    // M3 Task 20 continuation (authforge-sdk-refactor): the explicit
    // OAuth2StandardController::initApiDocs() call that used to live here
    // was removed to break a circular dependency -- OAuth2StandardController
    // now lives in libs/drogon, which itself links OAuth2Plugin (for the
    // not-yet-relocated common::error machinery; see
    // libs/drogon/CMakeLists.txt). OAuth2Plugin therefore cannot #include
    // anything from libs/drogon without creating a link cycle.
    // initApiDocs() is still called explicitly and unconditionally by every
    // production/test entry point before drogon::app().run() (see
    // OAuth2Server/main.cc and OAuth2Server/test/test_main.cc), and is
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
    static oauth2::adapters::DrogonLogger jwkManagerLogger;
    auto jwkManager = std::make_shared<oauth2::JwkManager>(&jwkManagerLogger);
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

    // Initialize Services
    // Defect 1.3 fix: services now share ownership of storage_ (shared_ptr),
    // so the storage lifetime is guaranteed to cover every service instead of
    // relying on the implicit "storage_ outlives services" timing convention.
    tokenService_ = std::make_shared<oauth2::TokenService>(
      storage_, authCodeTtl_, accessTokenTtl_, refreshTokenTtl_, issuer
    );
    tokenService_->setJwkManager(jwkManager_);
    clientService_ = std::make_shared<oauth2::ClientService>(storage_);
    identityService_ = std::make_shared<oauth2::IdentityService>(storage_);
    roleProvider_ = std::make_shared<oauth2::adapters::StorageRoleProvider>(storage_);

    // Initialize Cleanup Service
    cleanupService_ = std::make_shared<oauth2::OAuth2CleanupService>(storage_);
    double cleanupInterval = config.get("cleanup_interval_seconds", 3600.0).asDouble();
    cleanupService_->start(cleanupInterval);

    LOG_INFO << "OAuth2Plugin initialized with storage type: " << storageType_;
}

void OAuth2Plugin::initStorage(const Json::Value &config)
{
    storageType_ = config.get("storage_type", "memory").asString();

    if (storageType_ == "postgres")
    {
        // Option B (defect 1.8 nested ownership): create the inner Postgres via
        // std::make_shared from the CONCRETE type so its control block is bound
        // to PostgresOAuth2Storage. This arms enable_shared_from_this on the
        // inner storage so its own shared_from_this() is valid in BOTH roles:
        //   * wrapped as CachedOAuth2Storage::impl_ (shared_ptr), and
        //   * direct (the no-cache fallback below).
        auto pg = std::make_shared<oauth2::PostgresOAuth2Storage>();
        pg->initFromConfig(config["postgres"]);
        try
        {
            auto redis = drogon::app().getRedisClient("default");
            // The OUTER storage_ is created via make_shared<CachedOAuth2Storage>
            // so the shared_ptr control block binds the CONCRETE type
            // (CachedOAuth2Storage), arming its enable_shared_from_this. The
            // inner Postgres (pg) is handed in as a shared_ptr<IOAuth2Storage>
            // impl_, keeping its own armed control block (Option B). Do NOT move
            // a unique_ptr<IOAuth2Storage> into the shared_ptr — that binds the
            // control block to the base class and makes shared_from_this() throw
            // bad_weak_ptr.
            storage_ = std::make_shared<oauth2::CachedOAuth2Storage>(pg, redis);
            LOG_INFO << "Using PostgreSQL storage backend with L2 Redis Cache";
        }
        catch (...)
        {
            LOG_ERROR << "Failed to init Cache. Fallback to Postgres without cache.";
            // Direct (un-wrapped) Postgres: reuse the same make_shared'd concrete
            // instance, so its control block (and shared_from_this) stay valid.
            storage_ = pg;
        }
    }
    else if (storageType_ == "redis")
    {
        // Direct Redis: create via make_shared from the concrete type so the
        // control block binds RedisOAuth2Storage (arms shared_from_this in 8.1).
        std::string clientName = config["redis"].get("client_name", "default").asString();
        storage_ = std::make_shared<oauth2::RedisOAuth2Storage>(clientName);
    }
    else
    {
        // Direct Memory: create via make_shared from the concrete type.
        auto s = std::make_shared<oauth2::MemoryOAuth2Storage>();
        if (config.isMember("clients"))
            s->initFromConfig(config["clients"], config["admin_users"]);
        storage_ = std::move(s);
    }
}

void OAuth2Plugin::shutdown()
{
    // Explicit destruction order (defect 1.3 fix): stop the cleanup timer
    // first so no new cleanup callback is dispatched, then release the service
    // objects, and finally drop our reference to the storage. Because the
    // services now share ownership of storage_ (shared_ptr), releasing them
    // before resetting storage_ guarantees the storage outlives every service.
    // Any in-flight async callback that captured a service / storage shared_ptr
    // (added in tasks 8.x) keeps the relevant object alive until it completes,
    // so the storage is destroyed only after the last user is gone.
    //
    // Defect 1.8 (self-capture interaction): the storage classes
    // (CachedOAuth2Storage / RedisOAuth2Storage / PostgresOAuth2Storage) now
    // capture `auto self = shared_from_this();` in their async continuations.
    // As a result, when storage_.reset() runs here it may only drop OUR
    // reference; if a Redis/DB callback is still in flight, the last reference
    // is held by that continuation and ~CachedOAuth2Storage (and the inner
    // storage) will run on the redis/DB client's loop thread once the callback
    // completes — not on this shutdown thread. The shared-ownership guarantee
    // makes that deferred destruction safe (no member is touched through a
    // dangling `this`). We intentionally do not add extra synchronization here:
    // the strong `self` reference is the lifetime contract.
    if (cleanupService_)
        cleanupService_->stop();

    cleanupService_.reset();
    tokenService_.reset();
    clientService_.reset();
    identityService_.reset();

    storage_.reset();
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
  std::function<void(bool, std::string, std::string)> &&callback
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
      std::move(callback)
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
    tokenService_->refreshAccessToken(refreshToken, clientId, std::move(callback));
}

void OAuth2Plugin::validateAccessToken(
  const std::string &token,
  std::function<void(std::shared_ptr<AccessToken>)> &&callback
)
{
    tokenService_->validateAccessToken(token, std::move(callback));
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
  std::function<void(std::optional<oauth2::TokenIntrospection>)> &&callback
)
{
    tokenService_->introspectToken(token, std::move(callback));
}

void OAuth2Plugin::incrementIntrospectCount(
  const std::string &token,
  std::function<void()> &&callback
)
{
    if (storage_)
        storage_->incrementIntrospectCount(token, std::move(callback));
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
    // Keeping this static for convenience but could delegate to TokenService if needed
    // For now, it's just a pure function. Let's redirect to a helper if needed or keep static
    // logic. Actually, TokenService has a private version. Let's make it a public static utility in
    // common if needed. For now, I'll just re-implement or delegate to a private instance of
    // TokenService if possible. But since it's static in Plugin, I'll just re-implement briefly or
    // move to common/utils.
    return oauth2::TokenService(nullptr)
      .validatePkceCodeVerifier(codeVerifier, codeChallenge, codeChallengeMethod);
}

std::string OAuth2Plugin::generateSha256Hash(const std::string &input)
{
    return oauth2::TokenService(nullptr).generateSha256Hash(input);
}

bool OAuth2Plugin::scopeRequiresAdminRole(const std::string &scope)
{
    return oauth2::IdentityService(nullptr).scopeRequiresAdminRole(scope);
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
