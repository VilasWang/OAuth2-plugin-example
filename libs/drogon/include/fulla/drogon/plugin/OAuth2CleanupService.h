#pragma once

#include <drogon/drogon.h>
#include <fulla/oauth2/repository/IGrantRepository.h>
#include <fulla/oauth2/repository/ITokenRepository.h>
#include <memory>

namespace fulla::drogon
{

// Phase 4.2 (fulla-sdk-refactor): this service now depends on the NEW
// split-repository interfaces (fulla::oauth2::repository::*) instead of the
// god IOAuth2Storage facade. Per design.md §7 / Task 7 (A3), the old
// IOAuth2Storage::deleteExpiredData() is decomposed into per-repository
// purgeExpired() (one on IGrantRepository, one on ITokenRepository); this
// service orchestrates both. The distributed-lock + weak_ptr lifetime pattern
// (defects 1.3/1.10) is preserved verbatim.
class OAuth2CleanupService : public std::enable_shared_from_this<OAuth2CleanupService>
{
  public:
    OAuth2CleanupService(
      std::shared_ptr<::fulla::oauth2::repository::IGrantRepository> grantRepo,
      std::shared_ptr<::fulla::oauth2::repository::ITokenRepository> tokenRepo
    );
    ~OAuth2CleanupService();

    void start(double intervalSeconds);
    void stop();

  private:
    std::shared_ptr<::fulla::oauth2::repository::IGrantRepository> grantRepo_;
    std::shared_ptr<::fulla::oauth2::repository::ITokenRepository> tokenRepo_;
    uint64_t timerId_ = 0;
    bool running_ = false;
    bool stopped_ = false;  // Track if stop() has been called
    double interval_ = 3600;

    void runCleanup();
    // Phase 4.2: orchestrates grant + token purgeExpired() (the split of the
    // old IOAuth2Storage::deleteExpiredData, per design.md §7 / Task 7 A3).
    void doPurge();
};

}  // namespace fulla::drogon
