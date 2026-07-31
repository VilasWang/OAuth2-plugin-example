#include <drogon/drogon_test.h>
#include <drogon/orm/DbClient.h>
#include <drogon/drogon.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <iostream>

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
