#pragma once

// M3 Task 25 (authforge-sdk-refactor): extracted from main.cc's inline
// migrations-directory-lookup + auto-migrate block. Locates the SQL
// migrations directory (tries several relative paths) and, if
// OAUTH2_AUTO_MIGRATE=true, registers a beginning advice that runs
// schema::SchemaManager::migrate() on a detached thread shortly after
// startup.

namespace bootstrap
{

// Locates the migrations directory and registers the auto-migration
// beginning advice (opt-in via the OAUTH2_AUTO_MIGRATE=true environment
// variable). Must be called before drogon::app().run().
void setupMigrations();

}  // namespace bootstrap
