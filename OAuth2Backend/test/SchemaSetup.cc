#include <drogon/drogon_test.h>
#include <drogon/orm/DbClient.h>
#include <iostream>

using namespace drogon::orm;

DROGON_TEST(SchemaSetup)
{
    auto dbClient = DbClient::newPgClient("host=127.0.0.1 port=5432 dbname=oauth_test user=test password=123456", 1);
    
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
    try {
        dbClient->execSqlSync(sql);
        LOG_INFO << "SchemaSetup: users table created (or verified).";
    } catch (const DrogonDbException &e) {
        LOG_ERROR << "SchemaSetup Error: " << e.base().what();
        FAIL("Schema Creation Failed");
    }
}
