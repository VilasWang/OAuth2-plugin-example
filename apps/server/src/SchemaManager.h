#pragma once

#include <drogon/drogon.h>
#include <string>
#include <vector>

namespace schema
{

/**
 * @brief Simple schema migration manager for PostgreSQL
 *
 * On startup:
 * 1. Ensures `schema_migrations` table exists
 * 2. Scans `sql/migrations/` directory for V*.sql files
 * 3. Checks which versions are already applied
 * 4. Executes unapplied migrations in order
 * 5. Records each applied migration
 */
class SchemaManager
{
  public:
    /**
     * @brief Run all pending migrations
     * @param migrationsDir Path to the directory containing V*.sql files
     * @return true if all migrations applied successfully, false on error
     */
    static bool migrate(const std::string &migrationsDir);

    /**
     * @brief Run all pending migrations against an explicit DbClient.
     *
     * Task 37 (authforge-sdk-refactor): the K8s pre-install/pre-upgrade hook
     * Job runs `authforge-server --migrate-only`, which constructs its own
     * DbClient from config instead of relying on drogon::app().run() having
     * initialized the framework-owned client pool.
     */
    static bool migrate(const drogon::orm::DbClientPtr &db, const std::string &migrationsDir);

    /**
     * @brief Count migration files on disk not yet recorded in
     * schema_migrations (startup self-check for OAUTH2_AUTO_MIGRATE=false
     * deployments where migrations run externally via the hook Job).
     * @return number of pending migrations, or -1 on query/scan error
     */
    static int countPendingMigrations(
      const drogon::orm::DbClientPtr &db,
      const std::string &migrationsDir
    );

    /**
     * @brief Split a multi-statement SQL script into individual statements.
     *
     * PostgreSQL prepared statements accept only one statement, so migration
     * files (which may contain many statements) must be split on top-level
     * semicolons. The scanner tracks lexical state to avoid splitting inside
     * single-quoted strings, line/block comments, and dollar-quoted bodies
     * ($$..$$ / $tag$..$tag$). Empty/whitespace-only statements are skipped.
     *
     * Exposed publicly so unit tests can exercise it directly.
     */
    static std::vector<std::string> splitSqlStatements(const std::string &sql);

  private:
    struct MigrationFile
    {
        int version;
        std::string filename;
        std::string filepath;
    };

    /**
     * @brief Ensure the schema_migrations tracking table exists
     */
    static bool ensureMigrationsTable(const drogon::orm::DbClientPtr &db);

    /**
     * @brief Scan directory for V*.sql migration files
     */
    static std::vector<MigrationFile> scanMigrationFiles(const std::string &dir);

    /**
     * @brief Get set of already-applied migration versions
     */
    static std::vector<int> getAppliedVersions(const drogon::orm::DbClientPtr &db);

    /**
     * @brief Execute a single migration file and record it.
     *
     * Runs against an already-open transaction (the single transaction owned
     * by migrate()), so that all migrations in a run share one connection and
     * one transaction. This fixes #46: per-migration transactions could run on
     * different pool connections, and a later migration's transaction snapshot
     * failed to see an earlier migration's committed tables.
     *
     * Does NOT commit or roll back -- the caller owns the transaction lifetime.
     */
    static bool applyMigration(
      const drogon::orm::DbClientPtr &trans,
      const MigrationFile &migration
    );

    /**
     * @brief Compute SHA-256 checksum of file content
     */
    static std::string computeChecksum(const std::string &content);
};

}  // namespace schema
