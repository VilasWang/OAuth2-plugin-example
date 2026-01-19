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
    if(!db) {
        LOG_WARN << "DB Client unavailable, skipping UserSystemTest";
        return;
    }

    // Clean up
    try {
        db->execSqlSync("DELETE FROM users WHERE username = $1", "unittest_user_orm");
    } catch (...) {}

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
    try {
        mapper.insert(newUser);
        LOG_INFO << "User inserted via ORM";
    } catch (const DrogonDbException &e) {
        LOG_ERROR << e.base().what();
        FAIL("ORM Insert Failed");
    }

    // 3. Verify (ORM)
    try {
        auto user = mapper.findOne(
            Criteria(drogon_model::oauth_test::Users::Cols::_username, CompareOperator::EQ, "unittest_user_orm")
        );
        
        std::string dbHash = user.getValueOfPasswordHash();
        std::string dbSalt = user.getValueOfSalt();
        
        // Check stored salt
        CHECK(dbSalt == salt);

        // Verify Hash
        std::string inputHash = utils::getSha256(password + dbSalt);
        
        // Normalize
        std::transform(dbHash.begin(), dbHash.end(), dbHash.begin(), ::tolower);
        std::transform(inputHash.begin(), inputHash.end(), inputHash.begin(), ::tolower);

        CHECK(dbHash == inputHash);
        LOG_INFO << "ORM Password verification successful";
    } catch (const DrogonDbException &e) {
        FAIL("User not found via ORM: " + std::string(e.base().what()));
    }

    // Clean up
    try {
        db->execSqlSync("DELETE FROM users WHERE username = $1", "unittest_user_orm");
    } catch (...) {}
}

using namespace drogon;

