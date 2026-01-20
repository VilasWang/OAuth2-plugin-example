#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include "../models/Users.h"
#include <drogon/utils/Utilities.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

using namespace drogon;
using namespace drogon::orm;

DROGON_TEST(UserSystemTest)
{
    trantor::Logger::setLogLevel(trantor::Logger::kTrace);
    auto db = app().getDbClient();
    if (!db)
    {
        LOG_WARN << "DB Client unavailable, skipping UserSystemTest";
        return;
    }

    // Fix Schema (Salt length issue: UUID is 36 chars, column was 32)
    try
    {
        db->execSqlSync("ALTER TABLE users ALTER COLUMN salt TYPE VARCHAR(36)");
        LOG_INFO << "Schema corrected: salt column resized to 36";
    }
    catch (...)
    {
        // Ignore if already done or permission error (shouldn't happen in test
        // env)
    }

    // Clean up
    try
    {
        db->execSqlSync("DELETE FROM users WHERE username = $1",
                        "unittest_user_orm");
    }
    catch (...)
    {
    }

    // 1. Create User Data (ORM)
    drogon_model::oauth_test::Users newUser;
    newUser.setUsername("unittest_user_orm");
    std::string password = "password123";
    std::string salt = utils::getUuid();
    std::string hash = utils::getSha256(password + salt);
    newUser.setPasswordHash(hash);
    newUser.setSalt(salt);
    newUser.setEmail("test_orm@example.com");

    auto mapper = Mapper<drogon_model::oauth_test::Users>(db);

    // 2. Insert (ORM)
    try
    {
        mapper.insert(newUser);
        LOG_INFO << "User inserted via ORM";
    }
    catch (const DrogonDbException &e)
    {
        LOG_ERROR << "ORM Insert Error: " << e.base().what();
        FAIL("ORM Insert Failed");
    }

    // Delay to ensure persistence
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Raw SQL Verification
    {
        auto result = db->execSqlSync("SELECT * FROM users WHERE username = $1",
                                      "unittest_user_orm");
        if (result.empty())
        {
            LOG_ERROR << "Raw SQL: User NOT found in database!";
        }
        else
        {
            LOG_INFO << "Raw SQL: User found! ID: "
                     << result[0]["id"].as<int>();
        }
    }

    // 3. Verify (ORM)
    try
    {
        LOG_INFO << "Attempting to find user...";
        auto user = mapper.findOne(
            Criteria(drogon_model::oauth_test::Users::Cols::_username,
                     CompareOperator::EQ,
                     "unittest_user_orm"));
        LOG_INFO << "User found via ORM!";

        std::string dbHash = user.getValueOfPasswordHash();
        std::string dbSalt = user.getValueOfSalt();

        CHECK(dbSalt == salt);

        std::string inputHash = utils::getSha256(password + dbSalt);
        std::transform(dbHash.begin(), dbHash.end(), dbHash.begin(), ::tolower);
        std::transform(inputHash.begin(),
                       inputHash.end(),
                       inputHash.begin(),
                       ::tolower);

        CHECK(dbHash == inputHash);
    }
    catch (const DrogonDbException &e)
    {
        LOG_ERROR << "FindOne Error: " << e.base().what();
        FAIL("User not found via ORM: " + std::string(e.base().what()));
    }

    // Clean up
    try
    {
        db->execSqlSync("DELETE FROM users WHERE username = $1",
                        "unittest_user_orm");
    }
    catch (...)
    {
    }
}

using namespace drogon;
