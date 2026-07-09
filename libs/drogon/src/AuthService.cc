#include <authforge/drogon/AuthService.h>
#include <authforge/storage/postgres/models/Users.h>
#include <authforge/storage/postgres/models/Roles.h>
#include <authforge/storage/postgres/models/UserRoles.h>
#include <oauth2/utils/PasswordHasher.h>
#include <oauth2/utils/EmailNormalizer.h>
#include <drogon/utils/Utilities.h>
#include <algorithm>

using namespace drogon;
using namespace ::drogon::orm;

namespace authforge::drogon::services
{

void AuthService::validateUser(
  const std::string &identifier,
  const std::string &password,
  std::function<void(std::optional<AuthResult>)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(std::optional<AuthResult>)>>(std::move(callback));
    try
    {
        auto mapper = Mapper<drogon_model::oauth2_db::Users>(app().getDbClient());

        // 登录标识分流：含 @ 视为 email（先归一再查），否则按 username 查
        // USERNAME_PATTERN 不允许 @，二者天然互斥
        bool isEmail = identifier.find('@') != std::string::npos;
        std::string lookupKey = isEmail ? oauth2::utils::normalizeEmail(identifier) : identifier;
        auto criteria =
          isEmail
            ? Criteria(drogon_model::oauth2_db::Users::Cols::_email, CompareOperator::EQ, lookupKey)
            : Criteria(
                drogon_model::oauth2_db::Users::Cols::_username, CompareOperator::EQ, lookupKey
              );

        // Find user by login identifier (email or username)
        mapper.findOne(
          criteria,
          [sharedCb, password, identifier](const drogon_model::oauth2_db::Users &user) {
              // Account lockout check
              auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch()
              )
                           .count();

              int64_t lockedUntil = 0;
              int failedCount = 0;
              try
              {
                  // These columns may not exist in older schemas
                  lockedUntil = user.getValueOfLockedUntil();
                  failedCount = user.getValueOfFailedLoginCount();
              }
              catch (...)
              {
              }

              if (lockedUntil > now)
              {
                  LOG_WARN << "Account locked for user: " << identifier << " until " << lockedUntil;
                  (*sharedCb)(std::nullopt);
                  return;
              }

              // Compute Hash using PasswordHasher (supports PBKDF2 + legacy SHA-256)
              std::string salt = user.getValueOfSalt();
              std::string dbHash = user.getValueOfPasswordHash();

              bool valid = oauth2::utils::PasswordHasher::verify(password, dbHash, salt);

              if (valid)
              {
                  // Reset failed login count on success
                  if (failedCount > 0)
                  {
                      auto db = app().getDbClient();
                      int userId = user.getValueOfId();
                      db->execSqlAsync(
                        "UPDATE users SET failed_login_count = 0, locked_until = 0 WHERE id = $1",
                        [](const ::drogon::orm::Result &) {},
                        [](const ::drogon::orm::DrogonDbException &) {},
                        userId
                      );
                  }

                  // Check if password hash needs upgrade to PBKDF2
                  if (oauth2::utils::PasswordHasher::needsRehash(dbHash))
                  {
                      // Async upgrade: rehash with PBKDF2
                      try
                      {
                          std::string newHash = oauth2::utils::PasswordHasher::hash(password);
                          auto db = app().getDbClient();
                          int userId = user.getValueOfId();
                          db->execSqlAsync(
                            "UPDATE users SET password_hash = $1, salt = '' WHERE id = $2",
                            [userId](const ::drogon::orm::Result &) {
                                LOG_INFO << "Upgraded password hash to PBKDF2 for user " << userId;
                            },
                            [userId](const ::drogon::orm::DrogonDbException &e) {
                                LOG_WARN << "Failed to upgrade password hash for user " << userId
                                         << ": " << e.base().what();
                            },
                            newHash,
                            userId
                          );
                      }
                      catch (const std::exception &e)
                      {
                          LOG_WARN << "Password rehash failed: " << e.what();
                      }
                  }

                  AuthResult result;
                  result.internalId = user.getValueOfId();
                  result.publicSub = user.getValueOfPublicSub();
                  try
                  {
                      result.emailVerified = user.getValueOfEmailVerified();
                  }
                  catch (...)
                  {
                  }
                  try
                  {
                      result.mfaEnabled = user.getValueOfMfaEnabled();
                  }
                  catch (...)
                  {
                  }
                  (*sharedCb)(result);
              }
              else
              {
                  // Login failed - increment failed count and potentially lock
                  int newFailedCount = failedCount + 1;
                  int64_t newLockedUntil = 0;

                  // Progressive backoff: 5 fails = 1min, 10 = 5min, 15 = 30min, 20+ = 1hr
                  if (newFailedCount >= 20)
                      newLockedUntil = now + 3600;
                  else if (newFailedCount >= 15)
                      newLockedUntil = now + 1800;
                  else if (newFailedCount >= 10)
                      newLockedUntil = now + 300;
                  else if (newFailedCount >= 5)
                      newLockedUntil = now + 60;

                  auto db = app().getDbClient();
                  int userId = user.getValueOfId();
                  db->execSqlAsync(
                    "UPDATE users SET failed_login_count = $1, locked_until = $2, "
                    "last_failed_login = $3 WHERE id = $4",
                    [](const ::drogon::orm::Result &) {},
                    [](const ::drogon::orm::DrogonDbException &) {},
                    newFailedCount,
                    newLockedUntil,
                    now,
                    userId
                  );

                  (*sharedCb)(std::nullopt);
              }
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_WARN << "Validate User Failed: " << e.base().what();
              (*sharedCb)(std::nullopt);
          }
        );
    }
    catch (const DrogonDbException &e)
    {
        LOG_WARN << "Validate User Init Failed: " << e.base().what();
        (*sharedCb)(std::nullopt);
    }
}

void AuthService::registerUser(
  const std::string &username,
  const std::string &password,
  const std::string &email,
  std::function<void(const std::string &errorCode)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const std::string &errorCode)>>(std::move(callback));
    // Hash Password with Argon2id
    std::string salt = "";  // Argon2id embeds its own salt
    std::string passwordHash;
    try
    {
        passwordHash = oauth2::utils::PasswordHasher::hash(password);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "Password hashing failed: " << e.what();
        (*sharedCb)("INTERNAL_ERROR");
        return;
    }

    drogon_model::oauth2_db::Users newUser;
    // username is optional in email-first model: leave NULL when absent
    // (CHECK constraint forbids empty string, so only set when non-empty)
    if (!username.empty())
        newUser.setUsername(username);
    newUser.setPasswordHash(passwordHash);
    newUser.setSalt(salt);
    if (!email.empty())
        newUser.setEmail(oauth2::utils::normalizeEmail(email));

    try
    {
        auto db = app().getDbClient();
        // Start Transaction? For now, just chain.

        auto mapper = Mapper<drogon_model::oauth2_db::Users>(db);

        // Async Insert
        mapper.insert(
          newUser,
          [sharedCb, db](const drogon_model::oauth2_db::Users &u) {
              // Assign Default Role "user"
              try
              {
                  auto roleMapper = Mapper<drogon_model::oauth2_db::Roles>(db);
                  roleMapper.findOne(
                    Criteria(
                      drogon_model::oauth2_db::Roles::Cols::_name, CompareOperator::EQ, "user"
                    ),
                    [sharedCb,
                     db,
                     userId = u.getValueOfId()](const drogon_model::oauth2_db::Roles &role) {
                        try
                        {
                            auto urMapper = Mapper<drogon_model::oauth2_db::UserRoles>(db);
                            drogon_model::oauth2_db::UserRoles ur;
                            ur.setUserId(userId);
                            ur.setRoleId(role.getValueOfId());

                            urMapper.insert(
                              ur,
                              [sharedCb](const drogon_model::oauth2_db::UserRoles &) {
                                  (*sharedCb)("");  // Success
                              },
                              [sharedCb](const DrogonDbException &e) {
                                  LOG_ERROR << "Assign Role Failed: " << e.base().what();
                                  (*sharedCb)("");  // Treat as success
                                                    // for now (User
                                                    // created), but log
                                                    // error
                              }
                            );
                        }
                        catch (...)
                        {
                            (*sharedCb)("");
                        }
                    },
                    [sharedCb](const DrogonDbException &e) {
                        LOG_ERROR << "Default Role 'user' not found: " << e.base().what();
                        (*sharedCb)("");  // User created w/o role
                    }
                  );
              }
              catch (...)
              {
                  (*sharedCb)("");
              }
          },
          [sharedCb](const DrogonDbException &e) {
              const std::string what = e.base().what();
              LOG_ERROR << "Register Failed: " << what;
              // Map the failing DB constraint to a structured Error_Code so the
              // controller can forward it verbatim. Username conflict is checked
              // before email so a simultaneous conflict reports username first.
              if (what.find("users_username_key") != std::string::npos)
                  (*sharedCb)("VALIDATION_USERNAME_TAKEN");
              else if (what.find("idx_users_email_unique") != std::string::npos)
                  (*sharedCb)("VALIDATION_EMAIL_TAKEN");
              else
                  (*sharedCb)("VALIDATION_INVALID_INPUT");  // unrecognized constraint
          }
        );
    }
    catch (const DrogonDbException &e)
    {
        LOG_ERROR << "Register Init Failed: " << e.base().what();
        (*sharedCb)("INTERNAL_ERROR");
    }
}

void AuthService::getUserInfo(
  int userId,
  std::function<void(std::optional<Json::Value> userInfo)> &&callback
)
{
    auto sharedCb = std::make_shared<std::function<void(std::optional<Json::Value> userInfo)>>(
      std::move(callback)
    );

    try
    {
        auto db = app().getDbClient();
        auto userMapper = Mapper<drogon_model::oauth2_db::Users>(db);

        userMapper.findByPrimaryKey(
          userId,
          [sharedCb, db, userId](const drogon_model::oauth2_db::Users &user) {
              // Fetch roles
              db->execSqlAsync(
                "SELECT r.name FROM roles r JOIN user_roles ur ON r.id = "
                "ur.role_id WHERE ur.user_id = $1",
                [sharedCb, user, userId](const Result &r) {
                    Json::Value json;
                    json["sub"] = user.getValueOfPublicSub();
                    // OIDC name claim: username preferred, fallback to email when absent
                    // (username is optional in email-first model)
                    std::string displayName = user.getValueOfUsername();
                    json["name"] = displayName.empty() ? user.getValueOfEmail() : displayName;
                    json["email"] = user.getValueOfEmail();

                    Json::Value roles(Json::arrayValue);
                    for (auto row : r)
                    {
                        roles.append(row["name"].as<std::string>());
                    }
                    json["roles"] = roles;

                    (*sharedCb)(json);
                },
                [sharedCb, user, userId](const DrogonDbException &e) {
                    // Error fetching roles, just return user info with
                    // empty roles
                    LOG_WARN << "Failed to fetch roles for user " << userId << ": "
                             << e.base().what();
                    Json::Value json;
                    json["sub"] = user.getValueOfPublicSub();
                    // OIDC name claim: username preferred, fallback to email when absent
                    // (username is optional in email-first model)
                    std::string displayName = user.getValueOfUsername();
                    json["name"] = displayName.empty() ? user.getValueOfEmail() : displayName;
                    json["email"] = user.getValueOfEmail();
                    json["roles"] = Json::Value(Json::arrayValue);
                    (*sharedCb)(json);
                },
                userId
              );
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_WARN << "Get User Info Failed: " << e.base().what();
              (*sharedCb)(std::nullopt);
          }
        );
    }
    catch (const DrogonDbException &e)
    {
        LOG_WARN << "Get User Info Init Failed: " << e.base().what();
        (*sharedCb)(std::nullopt);
    }
}

}  // namespace authforge::drogon::services
