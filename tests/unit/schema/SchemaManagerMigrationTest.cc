// tests/unit/schema/SchemaManagerMigrationTest.cc
//
// Integration test for SchemaManager::migrate() — regression guard for #46.
//
// #46 symptom: on a cold DB, V3's per-migration transaction failed to see
// the tables V2's transaction committed *in the same run* ("relation X does
// not exist"). Root cause: each migration opened its own transaction, which
// could run on a different pool connection, so a later migration's snapshot
// didn't see an earlier migration's committed DDL. Fix: the whole run now
// executes inside ONE transaction on ONE connection.
//
// This test reproduces the cross-migration visibility requirement directly:
// V1 creates a parent table, V2 creates a child table with a FOREIGN KEY to
// the parent. Under the old per-transaction code, V2 could fail with
// "relation ... does not exist" if its snapshot predated V1's commit. Under
// the single-transaction fix, V2 sees V1's table within the same transaction.
//
// Uses high, collision-resistant version numbers (94601/94602) and prefixed
// table names so it never touches the real schema_migrations rows or tables.
//
// _Requirements: cross-migration visibility within one migrate() run._

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "SchemaManager.h"

using schema::SchemaManager;

namespace
{
namespace fs = std::filesystem;

// Unique table names so this test never collides with real schema objects.
constexpr const char *kParentTable = "_smtest46_parent";
constexpr const char *kChildTable = "_smtest46_child";

// High version numbers that the real migration set will never reach.
constexpr int kVersion1 = 94601;
constexpr int kVersion2 = 94602;

// Write a migration file with the canonical V{NNN}__name.sql naming.
void writeMigration(const fs::path &dir, int version, const std::string &slug, const std::string &sql)
{
    // Pad to 3 digits to match scanMigrationFiles' V(\d+)__ pattern.
    std::string num = std::to_string(version);
    while (num.size() < 3)
        num = "0" + num;
    fs::path file = dir / ("V" + num + "__" + slug + ".sql");
    std::ofstream(file) << sql;
}

// Count rows in a table (returns -1 if the query throws — e.g. table absent).
long countRows(const drogon::orm::DbClientPtr &db, const std::string &qualified)
{
    try
    {
        auto r = db->execSqlSync("SELECT count(*) AS c FROM " + qualified);
        if (r.empty())
            return -1;
        return r[0]["c"].as<long>();
    }
    catch (...)
    {
        return -1;
    }
}
}  // namespace

DROGON_TEST(Integration_P1_Schema_Migrate_CrossMigrationVisibility_Issue46)
{
    // Skip in memory-storage mode (no DB).
    auto db = drogon::app().getDbClient();
    if (!db)
    {
        LOG_WARN << "DB client not available. Skipping SchemaManager migration test.";
        return;
    }

    // Clean up any leftovers from a prior (possibly failed) run so this test
    // is idempotent: drop the child first (FK), then the parent, and remove
    // our version rows from schema_migrations.
    try
    {
        db->execSqlSync("DROP TABLE IF EXISTS " + std::string(kChildTable) + " CASCADE");
        db->execSqlSync("DROP TABLE IF EXISTS " + std::string(kParentTable) + " CASCADE");
        db->execSqlSync("DELETE FROM schema_migrations WHERE version IN ($1, $2)",
                        kVersion1, kVersion2);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        // schema_migrations may not exist yet; the migrate() call below will
        // create it. Log and continue.
        LOG_WARN << "Pre-cleanup (non-fatal): " << e.base().what();
    }

    // Build a temp migration directory with two cross-dependent migrations.
    fs::path tmpDir = fs::temp_directory_path() / "smtest46_migrations";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);

    // V1: parent table. V2: child table with a FOREIGN KEY referencing V1's
    // table. This is the #46 pattern: V2 must see V1's committed DDL.
    writeMigration(tmpDir, kVersion1, "parent",
                   "CREATE TABLE " + std::string(kParentTable) +
                     " (id SERIAL PRIMARY KEY, val INTEGER NOT NULL);");
    writeMigration(tmpDir, kVersion2, "child_fk",
                   "CREATE TABLE " + std::string(kChildTable) +
                     " (id SERIAL PRIMARY KEY, parent_id INTEGER NOT NULL, "
                     "CONSTRAINT smtest46_fk FOREIGN KEY (parent_id) "
                     "REFERENCES " +
                     std::string(kParentTable) + "(id));");

    // The fix under test: both migrations run in one transaction/connection.
    bool ok = SchemaManager::migrate(db, tmpDir.string());
    CHECK(ok == true);

    // Both tables must exist (V2 saw V1's DDL).
    CHECK(countRows(db, kParentTable) == 0);
    CHECK(countRows(db, kChildTable) == 0);

    // Both versions recorded.
    try
    {
        auto r = db->execSqlSync("SELECT count(*) AS c FROM schema_migrations "
                                 "WHERE version IN ($1, $2)",
                                 kVersion1, kVersion2);
        CHECK(r[0]["c"].as<long>() == 2);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        FAIL("Failed to read back schema_migrations: " + std::string(e.base().what()));
    }

    // Idempotency: running migrate() again with the same dir is a no-op
    // (versions already applied) and must not error.
    bool ok2 = SchemaManager::migrate(db, tmpDir.string());
    CHECK(ok2 == true);

    // Cleanup our tables and version rows; leave the real schema untouched.
    try
    {
        db->execSqlSync("DROP TABLE IF EXISTS " + std::string(kChildTable) + " CASCADE");
        db->execSqlSync("DROP TABLE IF EXISTS " + std::string(kParentTable) + " CASCADE");
        db->execSqlSync("DELETE FROM schema_migrations WHERE version IN ($1, $2)",
                        kVersion1, kVersion2);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_WARN << "Post-test cleanup (non-fatal): " << e.base().what();
    }
    fs::remove_all(tmpDir);
}
