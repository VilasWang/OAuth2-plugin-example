#include "MigrationRunner.h"
#include <drogon/drogon.h>
#include "../SchemaManager.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

namespace bootstrap
{

void setupMigrations()
{
    std::filesystem::path migrationsDir;
    // Try relative paths from likely working directories
    // (apps/server/migrations in the repo; sql/migrations kept for the
    // docker image layout /app/sql/migrations)
    if (std::filesystem::exists("migrations"))
        migrationsDir = "migrations";
    else if (std::filesystem::exists("sql/migrations"))
        migrationsDir = "sql/migrations";
    else if (std::filesystem::exists("../migrations"))
        migrationsDir = "../migrations";
    else if (std::filesystem::exists("../../apps/server/migrations"))
        migrationsDir = "../../apps/server/migrations";
    else if (std::filesystem::exists("../../../apps/server/migrations"))
        migrationsDir = "../../../apps/server/migrations";

    if (migrationsDir.empty())
    {
        LOG_WARN << "Migrations directory not found, skipping schema migration";
        return;
    }

    LOG_INFO << "Schema migrations directory found: "
             << std::filesystem::absolute(migrationsDir).string();

    // Auto-migration is opt-in via OAUTH2_AUTO_MIGRATE=true
    // In production, use setup_database.bat or CI pipeline for migrations
    const char *autoMigrate = std::getenv("OAUTH2_AUTO_MIGRATE");
    if (!autoMigrate || std::string(autoMigrate) != "true")
    {
        LOG_INFO << "Auto-migration disabled. Set OAUTH2_AUTO_MIGRATE=true to enable.";
        return;
    }

    std::string migrationsDirStr = migrationsDir.string();
    drogon::app().registerBeginningAdvice([migrationsDirStr]() {
        // Run in a detached thread to avoid blocking the event loop
        std::thread([migrationsDirStr]() {
            // Small delay to ensure DB pool is ready
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (!schema::SchemaManager::migrate(migrationsDirStr))
            {
                LOG_ERROR << "Schema migration failed!";
            }
        }).detach();
    });
}

}  // namespace bootstrap
