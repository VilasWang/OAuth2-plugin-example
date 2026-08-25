#include <fulla/drogon/plugin/OAuth2CleanupService.h>
#include <drogon/drogon.h>

namespace fulla::drogon
{

OAuth2CleanupService::OAuth2CleanupService(
  std::shared_ptr<::fulla::oauth2::repository::IGrantRepository> grantRepo,
  std::shared_ptr<::fulla::oauth2::repository::ITokenRepository> tokenRepo
)
    : grantRepo_(std::move(grantRepo)), tokenRepo_(std::move(tokenRepo))
{
}

OAuth2CleanupService::~OAuth2CleanupService()
{
    // If shutdown() was called, stop() has already been executed
    // No need to call it again, which avoids accessing the Event loop during
    // teardown
    if (!stopped_ && running_)
    {
        LOG_WARN << "OAuth2CleanupService destroyed without explicit shutdown()";
    }
}

void OAuth2CleanupService::start(double intervalSeconds)
{
    if (running_)
        return;

    // Guard: only start if the event loop is available
    auto loop = ::drogon::app().getLoop();
    if (!loop)
    {
        LOG_WARN << "Event loop not available, OAuth2 Cleanup Service will "
                    "not start";
        return;
    }

    running_ = true;
    interval_ = intervalSeconds;
    LOG_INFO << "Starting OAuth2 Cleanup Service with interval: " << intervalSeconds << "s";

    // Use weak_ptr to prevent SegFaults if Service is destroyed before timer
    // triggers
    std::weak_ptr<OAuth2CleanupService> weakSelf = weak_from_this();

    timerId_ = loop->runEvery(interval_, [weakSelf]() {
        auto self = weakSelf.lock();
        if (self && self->running_)
        {
            self->runCleanup();
        }
    });
}

void OAuth2CleanupService::stop()
{
    // Guard: if already stopped, do nothing (prevents accessing Event loop
    // during teardown)
    if (stopped_)
        return;

    stopped_ = true;

    if (!running_)
        return;

    LOG_INFO << "Stopping OAuth2 Cleanup Service";
    running_ = false;

    // Safely attempt to invalidate timer if loop is still running
    // Use try-catch to handle cases where Event loop is already destroyed
    try
    {
        auto loop = ::drogon::app().getLoop();
        if (loop && timerId_ != 0 && loop->isRunning())
        {
            loop->invalidateTimer(timerId_);
            timerId_ = 0;
        }
    }
    catch (const std::exception &e)
    {
        LOG_WARN << "Exception while invalidating timer: " << e.what();
    }
    catch (...)
    {
        LOG_WARN << "Unknown exception while invalidating timer";
    }
}

void OAuth2CleanupService::runCleanup()
{
    if (!running_ || !grantRepo_ || !tokenRepo_)
        return;

    // Distributed lock: only one instance should run cleanup at a time
    // Try to acquire Redis lock (if Redis is available)
    try
    {
        auto redis = ::drogon::app().getRedisClient("default");
        // SET key value NX EX ttl (acquire lock with TTL)
        int lockTtl = static_cast<int>(interval_ * 0.8);  // 80% of interval
        if (lockTtl < 60)
            lockTtl = 60;

        // Defect 1.10 fix: the Redis callbacks may fire AFTER this service has
        // been destroyed (the command is dispatched on the Redis client's loop
        // and the reply arrives later). Capturing a raw `this` here — while
        // start()'s runEvery timer already uses weak_from_this() — was an
        // implementation inconsistency that could dereference a dangling `this`
        // (use-after-free on this->running_ / this->storage_). Capture a
        // weak_ptr and lock() at the top of each callback, matching start():
        // if the service is gone (or no longer running), skip the cleanup
        // safely. A periodic cleanup is droppable, so weak_ptr (skip-if-gone)
        // is the correct choice rather than shared_ptr (extend-lifetime).
        std::weak_ptr<OAuth2CleanupService> weakSelf = weak_from_this();

        redis->execCommandAsync(
          [weakSelf](const ::drogon::nosql::RedisResult &r) {
              auto self = weakSelf.lock();
              if (!self || !self->running_)
                  return;
              if (r.isNil())
              {
                  // Lock not acquired - another instance is running cleanup
                  LOG_DEBUG << "Cleanup lock not acquired (another instance is running)";
                  return;
              }
              // Lock acquired - proceed with cleanup
              LOG_INFO << "Running periodic data cleanup (lock acquired)...";
              try
              {
                  self->doPurge();
              }
              catch (const std::exception &e)
              {
                  // Recoverable: background task retries on the next tick.
                  LOG_WARN << "Error during OAuth2 cleanup: " << e.what();
              }
          },
          [weakSelf](const std::exception & /*e*/) {
              auto self = weakSelf.lock();
              if (!self || !self->running_)
                  return;
              // Redis not available - run cleanup anyway (single instance mode)
              LOG_INFO << "Running periodic data cleanup (no Redis lock)...";
              try
              {
                  self->doPurge();
              }
              catch (const std::exception &ex)
              {
                  // Recoverable: background task retries on the next tick.
                  LOG_WARN << "Error during OAuth2 cleanup: " << ex.what();
              }
          },
          "SET oauth2:cleanup:lock %s NX EX %d",
          "locked",
          lockTtl
        );
    }
    catch (...)
    {
        // Redis not configured - run cleanup without lock
        LOG_INFO << "Running periodic data cleanup (no Redis)...";
        try
        {
            doPurge();
        }
        catch (const std::exception &e)
        {
            // Recoverable: doPurge() already ran; next tick retries.
            LOG_WARN << "Error during OAuth2 cleanup: " << e.what();
        }
        catch (...)
        {
            // Recoverable: same swallow-and-continue as above.
            LOG_WARN << "Unknown error during OAuth2 cleanup";
        }
    }
}

void OAuth2CleanupService::doPurge()
{
    // Phase 4.2: the old IOAuth2Storage::deleteExpiredData() is now split into
    // per-repository purgeExpired() (design.md §7 / Task 7 A3). Both are
    // synchronous (no callback). Purge grants (auth codes + authorization
    // transactions) then tokens (access + refresh).
    if (grantRepo_)
        grantRepo_->purgeExpired();
    if (tokenRepo_)
        tokenRepo_->purgeExpired();
}

}  // namespace fulla::drogon
