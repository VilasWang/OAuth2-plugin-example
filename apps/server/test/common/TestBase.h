#pragma once

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <memory>
#include "test_categories.h"

namespace oauth2::test
{

/**
 * @brief RAII-style Transaction Guard for Tests
 * Ensures all database changes made during a test are rolled back.
 */
class TestTransaction
{
  public:
    explicit TestTransaction(const std::string &dbName = "default")
    {
        try
        {
            dbClient_ = drogon::app().getDbClient(dbName);
            if (dbClient_)
            {
                dbClient_->execSqlSync("BEGIN");
                active_ = true;
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR << "Failed to start test transaction: " << e.what();
        }
    }

    ~TestTransaction()
    {
        rollback();
    }

    void rollback()
    {
        if (active_ && dbClient_)
        {
            try
            {
                dbClient_->execSqlSync("ROLLBACK");
            }
            catch (const std::exception &e)
            {
                LOG_ERROR << "Failed to rollback test transaction: " << e.what();
            }
            active_ = false;
        }
    }

    // Explicit commit if needed (usually not for these tests)
    void commit()
    {
        if (active_ && dbClient_)
        {
            try
            {
                dbClient_->execSqlSync("COMMIT");
            }
            catch (const std::exception &e)
            {
                LOG_ERROR << "Failed to commit test transaction: " << e.what();
            }
            active_ = false;
        }
    }

  private:
    drogon::orm::DbClientPtr dbClient_;
    bool active_{false};
};

/**
 * @brief Base class or utilities for tests
 */
class TestBase
{
  public:
    static void setup()
    {
        // Global setup logic if needed
    }

    static void teardown()
    {
        // Global teardown logic if needed
    }
};

}  // namespace oauth2::test
