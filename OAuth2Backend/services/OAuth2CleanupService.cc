#include "OAuth2CleanupService.h"

namespace oauth2
{

OAuth2CleanupService::OAuth2CleanupService(IOAuth2Storage *storage)
    : storage_(storage)
{
}

OAuth2CleanupService::~OAuth2CleanupService()
{
    stop();
}

void OAuth2CleanupService::start(double intervalSeconds)
{
    if (running_)
        return;

    running_ = true;
    LOG_INFO << "Starting OAuth2 Cleanup Service with interval: "
             << intervalSeconds << "s";

    // Immediate cleanup on start? Maybe not to avoid slow startup.
    // Allow loop to handle it.

    timerId_ = drogon::app().getLoop()->runEvery(intervalSeconds,
                                                 [this]() { runCleanup(); });
}

void OAuth2CleanupService::stop()
{
    if (running_ && timerId_ > 0)
    {
        LOG_INFO << "Stopping OAuth2 Cleanup Service";
        drogon::app().getLoop()->invalidateTimer(timerId_);
        timerId_ = 0;
    }
    running_ = false;
}

void OAuth2CleanupService::runCleanup()
{
    if (!running_ || !storage_)
        return;

    LOG_DEBUG << "Running periodic data cleanup...";
    try
    {
        storage_->deleteExpiredData();
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "Error during OAuth2 cleanup: " << e.what();
    }
    catch (...)
    {
        LOG_ERROR << "Unknown error during OAuth2 cleanup";
    }
}

}  // namespace oauth2
