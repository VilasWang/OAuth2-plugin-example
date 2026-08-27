#pragma once

#include <drogon/drogon.h>
#include <fulla/oauth2/repository/IGrantRepository.h>
#include <fulla/oauth2/repository/ITokenRepository.h>
#include <functional>
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
//
// #83: each cleanup cycle additionally calls ensure_audit_partitions() (the
// V025 partition-maintenance SQL function) so the monthly audit_logs
// partition horizon keeps advancing without a cron. Only active when the
// caller injects a DbClient AND declares postgres storage (the getDbClient()
// call is a process-terminating assert in memory mode, so this service must
// never resolve the client itself).
class OAuth2CleanupService : public std::enable_shared_from_this<OAuth2CleanupService>
{
  public:
    OAuth2CleanupService(
      std::shared_ptr<::fulla::oauth2::repository::IGrantRepository> grantRepo,
      std::shared_ptr<::fulla::oauth2::repository::ITokenRepository> tokenRepo,
      ::drogon::orm::DbClientPtr auditDbClient = nullptr,
      bool postgresStorage = false
    );
    ~OAuth2CleanupService();

    void start(double intervalSeconds);
    void stop();

    /// #83: run SELECT ensure_audit_partitions() (idempotent; creates missing
    /// monthly partitions and evacuates the DEFAULT partition). Fire-and-
    /// forget with logging; failures never block the cleanup cycle. Public as
    /// the integration-test seam. `completion` (optional) always fires.
    void maintainAuditPartitions(std::function<void()> &&completion = nullptr);

  private:
    std::shared_ptr<::fulla::oauth2::repository::IGrantRepository> grantRepo_;
    std::shared_ptr<::fulla::oauth2::repository::ITokenRepository> tokenRepo_;
    ::drogon::orm::DbClientPtr auditDbClient_;
    bool postgresStorage_ = false;
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
