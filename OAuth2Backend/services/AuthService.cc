#include "AuthService.h"
#include "../models/Users.h"
#include "../models/Roles.h"
#include "../models/UserRoles.h"
#include <drogon/utils/Utilities.h>
#include <algorithm>

using namespace drogon;
using namespace drogon::orm;

namespace services
{

void AuthService::validateUser(const std::string &username,
                               const std::string &password,
                               std::function<void(bool isValid)> &&callback)
{
    try
    {
        auto mapper =
            Mapper<drogon_model::oauth_test::Users>(app().getDbClient());

        // Find user by username
        mapper.findOne(
            {drogon_model::oauth_test::Users::Cols::_username,
             CompareOperator::EQ,
             username},
            [callback = std::move(callback),
             password](const drogon_model::oauth_test::Users &user) {
                // Compute Hash
                std::string salt = user.getValueOfSalt();
                std::string dbHash = user.getValueOfPasswordHash();
                std::string inputHash = utils::getSha256(password + salt);

                // Compare (Case insensitive for Hex)
                bool valid = false;
                if (inputHash.length() == dbHash.length())
                {
                    std::string inputLower = inputHash;
                    std::string dbLower = dbHash;

                    std::transform(inputLower.begin(),
                                   inputLower.end(),
                                   inputLower.begin(),
                                   ::tolower);
                    std::transform(dbLower.begin(),
                                   dbLower.end(),
                                   dbLower.begin(),
                                   ::tolower);

                    if (inputLower == dbLower)
                        valid = true;
                }
                callback(valid);
            },
            [callback](const DrogonDbException &e) {
                LOG_WARN << "Validate User Failed: " << e.base().what();
                callback(false);
            });
    }
    catch (const DrogonDbException &e)
    {
        LOG_WARN << "Validate User Init Failed: " << e.base().what();
        callback(false);
    }
}

void AuthService::registerUser(
    const std::string &username,
    const std::string &password,
    const std::string &email,
    std::function<void(const std::string &error)> &&callback)
{
    // Hash Password
    std::string salt = utils::getUuid();
    std::string passwordHash = utils::getSha256(password + salt);

    drogon_model::oauth_test::Users newUser;
    newUser.setUsername(username);
    newUser.setPasswordHash(passwordHash);
    newUser.setSalt(salt);
    if (!email.empty())
        newUser.setEmail(email);

    try
    {
        auto db = app().getDbClient();
        // Start Transaction? For now, just chain.

        auto mapper = Mapper<drogon_model::oauth_test::Users>(db);

        // Async Insert
        mapper.insert(
            newUser,
            [db, callback](const drogon_model::oauth_test::Users &u) {
                // Assign Default Role "user"
                try
                {
                    auto roleMapper =
                        Mapper<drogon_model::oauth_test::Roles>(db);
                    roleMapper.findOne(
                        Criteria(drogon_model::oauth_test::Roles::Cols::_name,
                                 CompareOperator::EQ,
                                 "user"),
                        [db, callback, userId = u.getValueOfId()](
                            const drogon_model::oauth_test::Roles &role) {
                            try
                            {
                                auto urMapper =
                                    Mapper<drogon_model::oauth_test::UserRoles>(
                                        db);
                                drogon_model::oauth_test::UserRoles ur;
                                ur.setUserId(userId);
                                ur.setRoleId(role.getValueOfId());

                                urMapper.insert(
                                    ur,
                                    [callback](const drogon_model::oauth_test::
                                                   UserRoles &) {
                                        callback("");  // Success
                                    },
                                    [callback](const DrogonDbException &e) {
                                        LOG_ERROR << "Assign Role Failed: "
                                                  << e.base().what();
                                        callback("");  // Treat as success for
                                                       // now (User created),
                                                       // but log error
                                    });
                            }
                            catch (...)
                            {
                                callback("");
                            }
                        },
                        [callback](const DrogonDbException &e) {
                            LOG_ERROR << "Default Role 'user' not found: "
                                      << e.base().what();
                            callback("");  // User created w/o role
                        });
                }
                catch (...)
                {
                    callback("");
                }
            },
            [callback](const DrogonDbException &e) {
                LOG_ERROR << "Register Failed: " << e.base().what();
                // Usually duplicate username
                callback("Registration Failed (Username likely exists)");
            });
    }
    catch (const DrogonDbException &e)
    {
        LOG_ERROR << "Register Init Failed: " << e.base().what();
        callback("Internal Server Error");
    }
}

}  // namespace services
