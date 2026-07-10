#include <authforge/storage/postgres/PostgresIdentityRepository.h>

#include <authforge/storage/postgres/models/Users.h>
#include <authforge/storage/postgres/models/Roles.h>
#include <authforge/storage/postgres/models/UserRoles.h>
#include <authforge/storage/postgres/models/Oauth2SubjectMappings.h>

#include <drogon/drogon.h>

namespace authforge::storage::postgres
{

using namespace drogon::orm;
using namespace drogon_model::oauth2_db;
using authforge::identity::UserData;

namespace
{

UserData toUserData(const Users &row)
{
    UserData data;
    data.id = row.getValueOfId();
    data.username = row.getValueOfUsername();
    data.email = row.getValueOfEmail();
    data.passwordHash = row.getValueOfPasswordHash();
    data.salt = row.getValueOfSalt();
    data.publicSub = row.getValueOfPublicSub();
    try
    {
        data.emailVerified = row.getValueOfEmailVerified();
    }
    catch (...)
    {
    }
    try
    {
        data.mfaEnabled = row.getValueOfMfaEnabled();
    }
    catch (...)
    {
    }
    try
    {
        data.lockedUntil = row.getValueOfLockedUntil();
    }
    catch (...)
    {
    }
    try
    {
        data.failedLoginCount = row.getValueOfFailedLoginCount();
    }
    catch (...)
    {
    }
    return data;
}

}  // namespace

void PostgresIdentityRepository::findByEmail(
  const std::string &email,
  std::function<void(std::optional<UserData>)> &&callback
)
{
    if (!dbClient_)
    {
        callback(std::nullopt);
        return;
    }
    auto sharedCb =
      std::make_shared<std::function<void(std::optional<UserData>)>>(std::move(callback));
    try
    {
        Mapper<Users> mapper(dbClient_);
        mapper.findOne(
          Criteria(Users::Cols::_email, CompareOperator::EQ, email),
          [sharedCb](const Users &row) { (*sharedCb)(toUserData(row)); },
          [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
        );
    }
    catch (...)
    {
        (*sharedCb)(std::nullopt);
    }
}

void PostgresIdentityRepository::findByUsername(
  const std::string &username,
  std::function<void(std::optional<UserData>)> &&callback
)
{
    if (!dbClient_)
    {
        callback(std::nullopt);
        return;
    }
    auto sharedCb =
      std::make_shared<std::function<void(std::optional<UserData>)>>(std::move(callback));
    try
    {
        Mapper<Users> mapper(dbClient_);
        mapper.findOne(
          Criteria(Users::Cols::_username, CompareOperator::EQ, username),
          [sharedCb](const Users &row) { (*sharedCb)(toUserData(row)); },
          [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
        );
    }
    catch (...)
    {
        (*sharedCb)(std::nullopt);
    }
}

void PostgresIdentityRepository::findById(
  int64_t userId,
  std::function<void(std::optional<UserData>)> &&callback
)
{
    if (!dbClient_)
    {
        callback(std::nullopt);
        return;
    }
    auto sharedCb =
      std::make_shared<std::function<void(std::optional<UserData>)>>(std::move(callback));
    try
    {
        Mapper<Users> mapper(dbClient_);
        mapper.findOne(
          Criteria(Users::Cols::_id, CompareOperator::EQ, static_cast<int32_t>(userId)),
          [sharedCb](const Users &row) { (*sharedCb)(toUserData(row)); },
          [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
        );
    }
    catch (...)
    {
        (*sharedCb)(std::nullopt);
    }
}

void PostgresIdentityRepository::create(
  const UserData &userData,
  std::function<void(std::optional<int64_t>, std::string)> &&callback
)
{
    if (!dbClient_)
    {
        callback(std::nullopt, "INTERNAL_ERROR");
        return;
    }

    Users newUser;
    if (!userData.username.empty())
        newUser.setUsername(userData.username);
    newUser.setPasswordHash(userData.passwordHash);
    newUser.setSalt(userData.salt);
    if (!userData.email.empty())
        newUser.setEmail(userData.email);

    auto sharedCb =
      std::make_shared<std::function<void(std::optional<int64_t>, std::string)>>(
        std::move(callback)
      );
    auto db = dbClient_;

    try
    {
        Mapper<Users> mapper(db);
        mapper.insert(
          newUser,
          [sharedCb, db](const Users &inserted) {
              int64_t newUserId = inserted.getValueOfId();
              // Assign default "user" role, mirroring
              // OAuth2Server/AuthService.cc::registerUser's existing
              // behavior. A role-assignment failure is logged but does
              // not fail user creation (same tolerance as the original).
              try
              {
                  Mapper<Roles> roleMapper(db);
                  roleMapper.findOne(
                    Criteria(Roles::Cols::_name, CompareOperator::EQ, "user"),
                    [sharedCb, db, newUserId](const Roles &role) {
                        try
                        {
                            Mapper<UserRoles> urMapper(db);
                            UserRoles ur;
                            ur.setUserId(static_cast<int32_t>(newUserId));
                            ur.setRoleId(role.getValueOfId());
                            urMapper.insert(
                              ur,
                              [sharedCb, newUserId](const UserRoles &) {
                                  (*sharedCb)(newUserId, "");
                              },
                              [sharedCb, newUserId](const DrogonDbException &e) {
                                  LOG_ERROR << "PostgresIdentityRepository::create: role "
                                               "assignment failed: "
                                            << e.base().what();
                                  (*sharedCb)(newUserId, "");
                              }
                            );
                        }
                        catch (...)
                        {
                            (*sharedCb)(newUserId, "");
                        }
                    },
                    [sharedCb, newUserId](const DrogonDbException &e) {
                        LOG_ERROR << "PostgresIdentityRepository::create: default role 'user' "
                                     "not found: "
                                  << e.base().what();
                        (*sharedCb)(newUserId, "");
                    }
                  );
              }
              catch (...)
              {
                  (*sharedCb)(newUserId, "");
              }
          },
          [sharedCb](const DrogonDbException &e) {
              const std::string what = e.base().what();
              LOG_ERROR << "PostgresIdentityRepository::create failed: " << what;
              // Classify the failing DB constraint into the same
              // structured Error_Codes OAuth2Server/AuthService.cc's
              // pre-migration registerUser produced (auth-flow-error-
              // code-gaps spec) -- username conflict checked first so a
              // simultaneous username+email conflict reports username.
              if (what.find("users_username_key") != std::string::npos)
                  (*sharedCb)(std::nullopt, "VALIDATION_USERNAME_TAKEN");
              else if (what.find("idx_users_email_unique") != std::string::npos)
                  (*sharedCb)(std::nullopt, "VALIDATION_EMAIL_TAKEN");
              else
                  (*sharedCb)(std::nullopt, "VALIDATION_INVALID_INPUT");
          }
        );
    }
    catch (...)
    {
        (*sharedCb)(std::nullopt, "INTERNAL_ERROR");
    }
}

void PostgresIdentityRepository::updatePasswordHash(
  int64_t userId,
  const std::string &newHash,
  std::function<void(bool)> &&callback
)
{
    if (!dbClient_)
    {
        callback(false);
        return;
    }
    auto sharedCb = std::make_shared<std::function<void(bool)>>(std::move(callback));
    dbClient_->execSqlAsync(
      "UPDATE users SET password_hash = $1, salt = '' WHERE id = $2",
      [sharedCb](const Result &) { (*sharedCb)(true); },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); },
      newHash,
      userId
    );
}

void PostgresIdentityRepository::resetFailedLogins(
  int64_t userId,
  std::function<void(bool)> &&callback
)
{
    if (!dbClient_)
    {
        callback(false);
        return;
    }
    auto sharedCb = std::make_shared<std::function<void(bool)>>(std::move(callback));
    dbClient_->execSqlAsync(
      "UPDATE users SET failed_login_count = 0, locked_until = 0 WHERE id = $1",
      [sharedCb](const Result &) { (*sharedCb)(true); },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); },
      userId
    );
}

void PostgresIdentityRepository::incrementFailedLogins(
  int64_t userId,
  std::function<void(bool)> &&callback
)
{
    if (!dbClient_)
    {
        callback(false);
        return;
    }
    auto sharedCb = std::make_shared<std::function<void(bool)>>(std::move(callback));
    auto db = dbClient_;

    // Read current failed_login_count first so the progressive-backoff
    // window matches AuthService.cc's existing thresholds (5/10/15/20+).
    db->execSqlAsync(
      "SELECT failed_login_count FROM users WHERE id = $1",
      [sharedCb, db, userId](const Result &r) {
          int failedCount = r.empty() ? 0 : r[0]["failed_login_count"].as<int>();
          int newFailedCount = failedCount + 1;
          int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch()
          )
                          .count();
          int64_t newLockedUntil = 0;
          if (newFailedCount >= 20)
              newLockedUntil = now + 3600;
          else if (newFailedCount >= 15)
              newLockedUntil = now + 1800;
          else if (newFailedCount >= 10)
              newLockedUntil = now + 300;
          else if (newFailedCount >= 5)
              newLockedUntil = now + 60;

          db->execSqlAsync(
            "UPDATE users SET failed_login_count = $1, locked_until = $2, "
            "last_failed_login = $3 WHERE id = $4",
            [sharedCb](const Result &) { (*sharedCb)(true); },
            [sharedCb](const DrogonDbException &) { (*sharedCb)(false); },
            newFailedCount,
            newLockedUntil,
            now,
            userId
          );
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); },
      userId
    );
}

void PostgresIdentityRepository::getUserInfoWithRoles(
  int64_t userId,
  std::function<void(std::optional<Json::Value>)> &&callback
)
{
    if (!dbClient_)
    {
        callback(std::nullopt);
        return;
    }
    auto sharedCb =
      std::make_shared<std::function<void(std::optional<Json::Value>)>>(std::move(callback));
    auto db = dbClient_;

    try
    {
        Mapper<Users> mapper(db);
        mapper.findOne(
          Criteria(Users::Cols::_id, CompareOperator::EQ, static_cast<int32_t>(userId)),
          [sharedCb, db, userId](const Users &user) {
              db->execSqlAsync(
                "SELECT r.name FROM roles r JOIN user_roles ur ON r.id = "
                "ur.role_id WHERE ur.user_id = $1",
                [sharedCb, user](const Result &r) {
                    Json::Value json;
                    json["sub"] = user.getValueOfPublicSub();
                    std::string displayName = user.getValueOfUsername();
                    json["name"] = displayName.empty() ? user.getValueOfEmail() : displayName;
                    json["email"] = user.getValueOfEmail();

                    Json::Value roles(Json::arrayValue);
                    for (auto row : r)
                        roles.append(row["name"].as<std::string>());
                    json["roles"] = roles;

                    (*sharedCb)(json);
                },
                [sharedCb, user](const DrogonDbException &) {
                    Json::Value json;
                    json["sub"] = user.getValueOfPublicSub();
                    std::string displayName = user.getValueOfUsername();
                    json["name"] = displayName.empty() ? user.getValueOfEmail() : displayName;
                    json["email"] = user.getValueOfEmail();
                    json["roles"] = Json::Value(Json::arrayValue);
                    (*sharedCb)(json);
                },
                userId
              );
          },
          [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
        );
    }
    catch (...)
    {
        (*sharedCb)(std::nullopt);
    }
}

void PostgresIdentityRepository::getRoles(
  int64_t internalUserId,
  std::function<void(std::vector<std::string>)> &&cb
)
{
    if (!dbClient_)
    {
        cb({});
        return;
    }
    auto sharedCb = std::make_shared<std::function<void(std::vector<std::string>)>>(std::move(cb));
    auto db = dbClient_;

    try
    {
        Mapper<UserRoles> urMapper(db);
        urMapper.findBy(
          Criteria(UserRoles::Cols::_user_id, CompareOperator::EQ, static_cast<int32_t>(internalUserId)),
          [sharedCb, db](const std::vector<UserRoles> &userRoles) {
              if (userRoles.empty())
              {
                  (*sharedCb)({});
                  return;
              }
              std::vector<int32_t> roleIds;
              for (const auto &ur : userRoles)
                  roleIds.push_back(ur.getValueOfRoleId());

              Mapper<Roles> roleMapper(db);
              roleMapper.findBy(
                Criteria(Roles::Cols::_id, CompareOperator::In, roleIds),
                [sharedCb](const std::vector<Roles> &roles) {
                    std::vector<std::string> names;
                    for (const auto &role : roles)
                        names.push_back(role.getValueOfName());
                    (*sharedCb)(names);
                },
                [sharedCb](const DrogonDbException &) { (*sharedCb)({}); }
              );
          },
          [sharedCb](const DrogonDbException &) { (*sharedCb)({}); }
        );
    }
    catch (...)
    {
        (*sharedCb)({});
    }
}

void PostgresIdentityRepository::getInternalUserId(
  const std::string &subject,
  const std::string &provider,
  std::function<void(std::optional<int64_t>)> &&cb
)
{
    if (!dbClient_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb =
      std::make_shared<std::function<void(std::optional<int64_t>)>>(std::move(cb));
    try
    {
        Mapper<Oauth2SubjectMappings> mapper(dbClient_);
        mapper.findOne(
          Criteria(Oauth2SubjectMappings::Cols::_provider, CompareOperator::EQ, provider) &&
            Criteria(Oauth2SubjectMappings::Cols::_subject, CompareOperator::EQ, subject),
          [sharedCb](const Oauth2SubjectMappings &mapping) {
              (*sharedCb)(static_cast<int64_t>(mapping.getValueOfInternalUserId()));
          },
          [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
        );
    }
    catch (...)
    {
        (*sharedCb)(std::nullopt);
    }
}

}  // namespace authforge::storage::postgres
