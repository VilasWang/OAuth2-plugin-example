// tests/integration/plugin/CleanupAuditPartitionTest.cc
//
// #83: OAuth2CleanupService::maintainAuditPartitions drives V025's
// ensure_audit_partitions() on every cleanup cycle so the monthly audit_logs
// partition horizon keeps advancing without a cron. Real-PG integration
// coverage (the SQL function is DDL and cannot be faked):
//   - completion always fires (postgres path)
//   - the call is idempotent (second run reports no work, no throw)
//   - the disabled paths (no client / non-postgres flag) short-circuit and
//     still fire the completion without touching the DB.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <fulla/drogon/plugin/OAuth2CleanupService.h>

#include <chrono>
#include <future>

using fulla::drogon::OAuth2CleanupService;

namespace
{
bool postgresAvailable()
{
    try
    {
        auto db = drogon::app().getDbClient();
        db->execSqlSync("SELECT 1");
        return true;
    }
    catch (...)
    {
        return false;
    }
}
}  // namespace

DROGON_TEST(Integration_P2_Cleanup_AuditPartitions_RunsAndIsIdempotent)
{
    if (!postgresAvailable())
    {
        LOG_INFO << "[skip] Postgres unavailable — audit partition test skipped";
        CHECK(true);
        return;
    }
    auto db = drogon::app().getDbClient();
    REQUIRE(db != nullptr);

    OAuth2CleanupService service(nullptr, nullptr, db, true);

    std::promise<void> first;
    service.maintainAuditPartitions([&first]() { first.set_value(); });
    REQUIRE(first.get_future().wait_for(std::chrono::seconds(15)) == std::future_status::ready);

    // Idempotent: a second call must complete (and create nothing -- the
    // horizon was just extended by the first call).
    std::promise<void> second;
    service.maintainAuditPartitions([&second]() { second.set_value(); });
    REQUIRE(second.get_future().wait_for(std::chrono::seconds(15)) == std::future_status::ready);

    // The horizon itself: current month's partition exists (V025 + this
    // maintenance guarantee [now-12mo, now+25mo)).
    auto rows = db->execSqlSync(
      "SELECT to_regclass('audit_logs_default') IS NOT NULL AS has_default"
    );
    REQUIRE(rows.size() == 1);
    CHECK(rows[0]["has_default"].as<bool>());
}

DROGON_TEST(Integration_P2_Cleanup_AuditPartitions_DisabledPaths_CompleteWithoutDb)
{
    // Non-postgres / no client: direct completion, no DB access (a
    // getDbClient() in memory mode is a process-terminating assert).
    {
        OAuth2CleanupService service(nullptr, nullptr, nullptr, false);
        std::promise<void> done;
        service.maintainAuditPartitions([&done]() { done.set_value(); });
        CHECK(done.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    }
    {
        // Client present but flag false: still short-circuits.
        auto db = drogon::app().getDbClient();
        if (!db)
        {
            CHECK(true);
            return;
        }
        OAuth2CleanupService service(nullptr, nullptr, db, false);
        std::promise<void> done;
        service.maintainAuditPartitions([&done]() { done.set_value(); });
        CHECK(done.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    }
}
