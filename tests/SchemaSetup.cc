#include <drogon/drogon_test.h>
#include <drogon/orm/DbClient.h>
#include <drogon/drogon.h>
#include <fulla/drogon/plugin/OAuth2Plugin.h>
#include <iostream>

#include "StorageSeed.h"  // tests/common/, in the test target's include dirs

using namespace drogon::orm;

DROGON_TEST(Database_P0_Schema_Setup_Works)
{
    // Skip this test in memory storage mode (no database)
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        LOG_INFO << "Skipping SchemaSetup in memory storage mode";
        return;
    }

    auto dbClient = drogon::app().getDbClient();
    if (!dbClient)
    {
        LOG_WARN << "DB client not available. Skipping Schema Setup.";
        return;
    }

    // Create users table
    std::string sql = R"(
        CREATE TABLE IF NOT EXISTS users (
            id SERIAL PRIMARY KEY,
            username VARCHAR(50) UNIQUE NOT NULL,
            password_hash VARCHAR(128) NOT NULL,
            salt VARCHAR(36) NOT NULL,
            email VARCHAR(100),
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )";

    // Synchronous execution for setup
    try
    {
        dbClient->execSqlSync(sql);
        LOG_INFO << "SchemaSetup: users table created (or verified).";
    }
    catch (const DrogonDbException &e)
    {
        LOG_ERROR << "SchemaSetup Error: " << e.base().what();
        FAIL("Schema Creation Failed");
    }
}

// Database seed for HTTP admin integration tests
// (docs/history/design/http-integration-test-coverage-plan.md, Phase 1, B1 fix).
//
// The test binary does not otherwise seed the admin user -- the dev seed SQL
// (apps/server/seed/dev_admin_user.sql etc.) is applied only by the Linux CI
// shell step, so running the binary directly (locally, or on Windows/macOS CI
// legs) left the DB without the admin user and every admin integration test
// silently fell through its memory-skip guard. This case runs the in-process
// seeder (tests/common/StorageSeed.h) -- migrations + dev seed files + admin
// lockout reset -- so admin-route tests work wherever Postgres is reachable.
//
// Order note: ctest runs DROGON_TEST cases in DrClassMap registration order,
// which is not guaranteed to match source order. The seeder is idempotent
// (migrations tracked in schema_migrations; seeds ON CONFLICT DO NOTHING), so
// running it more than once -- or after another test that already seeded --
// is a no-op. Admin integration tests additionally call loginAsAdmin(), which
// tolerates a not-yet-seeded DB by returning nullopt; but this case ensures
// seeding happens reliably and visibly in the test log.
DROGON_TEST(Database_P0_Seed_Setup_Works)
{
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        LOG_INFO << "Skipping in-process DB seed in memory storage mode";
        return;
    }

    const bool ok = fulla::test::seed::seedDatabase();
    if (!ok)
    {
        // A DB-configured but unreachable/seed-failed state is a real problem
        // for every admin test that follows -- surface it loudly rather than
        // letting dozens of downstream tests no-op mysteriously.
        FAIL("In-process DB seed failed (see [seed] log lines above)");
        return;
    }
    CHECK(ok);
    LOG_INFO << "In-process DB seed completed successfully.";
}
