#pragma once

#include <drogon/drogon.h>
#include <memory>
#include "../storage/IOAuth2Storage.h"

namespace oauth2
{

class OAuth2CleanupService
{
  public:
    OAuth2CleanupService(IOAuth2Storage *storage);
    ~OAuth2CleanupService();

    void start(double intervalSeconds);
    void stop();

  private:
    IOAuth2Storage *storage_;
    uint64_t timerId_ = 0;
    bool running_ = false;

    void runCleanup();
};

}  // namespace oauth2
