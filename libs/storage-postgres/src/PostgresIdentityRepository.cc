#include <authforge/storage/postgres/PostgresIdentityRepository.h>

#include <authforge/storage/postgres/models/Users.h>
#include <authforge/storage/postgres/models/Roles.h>
#include <authforge/storage/postgres/models/UserRoles.h>
#include <authforge/storage/postgres/models/Oauth2SubjectMappings.h>

#include <drogon/drogon.h>

namespace authforge::storage::postgres
{

using namespace ::drogon::orm;
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
  int32_t userId,
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
          Criteria(Users::Cols::_id, CompareOperator::EQ, userId),
          [sharedCb](const Users &row) { (*sharedCb)(toUserData(row)); },
          [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
        );
    }
    catch (...)
    {
        (*sharedCb)(std::nullopt);
    }
}

void PostgresIdentityRepository::findByPublicSub(
  const std::string &publicSub,
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
          Criteria(Users::Cols::_public_sub, CompareOperator::EQ, publicSub),
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
  std::function<void(std::optional<int32_t>, std::string)> &&callback
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

    auto sharedCb = std::make_shared<std::function<void(std::optional<int32_t>, std::string)>>(
      std::move(callback)
    );
    auto db = dbClient_;

    try
    {
        Mapper<Users> mapper(db);
        mapper.insert(
          newUser,
          [sharedCb, db](const Users &inserted) {
              int32_t newUserId = inserted.getValueOfId();
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
                            ur.setUserId(newUserId);
                            ur.setRoleId(role.getValueOfId());
                            urMapper.insert(
                              ur,
                              [sharedCb, newUserId](const UserRoles &) {
                                  (*sharedCb)(newUserId, "");
                              },
                              [sharedCb, newUserId](const DrogonDbException &e) {
                                  // Recoverable: user is already created; role
                                  // assignment is a side effect (callback reports
                                  // success with empty error string).
                                  LOG_WARN << "PostgresIdentityRepository::create: role "
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
                        // Recoverable: user created without a role.
                        LOG_WARN << "PostgresIdentityRepository::create: default role 'user' "
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
  int32_t userId,
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
    LOG_DEBUG << "[PG-Identity] updatePasswordHash: userId=" << userId;

    Mapper<Users> mapper(dbClient_);
    mapper.findBy(
      Criteria(Users::Cols::_id, CompareOperator::EQ, userId),
      [sharedCb, newHash, self = shared_from_this()](const std::vector<Users> &users) {
          if (users.empty())
          {
              (*sharedCb)(false);
              return;
          }
          auto updated = std::make_shared<Users>(users[0]);
          updated->setPasswordHash(newHash);
          updated->setSalt("");
          Mapper<Users>(self->dbClient_)
            .update(
              *updated,
              [updated, sharedCb](const size_t) { (*sharedCb)(true); },
              [updated, sharedCb](const DrogonDbException &e) {
                  LOG_WARN << "[PG-Identity] updatePasswordHash FAILED: " << e.base().what();
                  (*sharedCb)(false);
              }
            );
      },
      [sharedCb](const DrogonDbException &e) {
          LOG_WARN << "[PG-Identity] updatePasswordHash FAILED: " << e.base().what();
          (*sharedCb)(false);
      }
    );
}

void PostgresIdentityRepository::resetFailedLogins(
  int32_t userId,
  std::function<void(bool)> &&callback
)
{
    if (!dbClient_)
    {
        callback(false);
        return;
    }
    auto sharedCb = std::make_shared<std::function<void(bool)>>(std::move(callback));
    LOG_DEBUG << "[PG-Identity] resetFailedLogins: userId=" << userId;

    Mapper<Users> mapper(dbClient_);
    mapper.findBy(
      Criteria(Users::Cols::_id, CompareOperator::EQ, userId),
      [sharedCb, self = shared_from_this()](const std::vector<Users> &users) {
          if (users.empty())
          {
              (*sharedCb)(false);
              return;
          }
          auto updated = std::make_shared<Users>(users[0]);
          updated->setFailedLoginCount(0);
          updated->setLockedUntil(0);
          Mapper<Users>(self->dbClient_)
            .update(
              *updated,
              [updated, sharedCb](const size_t) { (*sharedCb)(true); },
              [updated, sharedCb](const DrogonDbException &e) {
                  LOG_WARN << "[PG-Identity] resetFailedLogins FAILED: " << e.base().what();
                  (*sharedCb)(false);
              }
            );
      },
      [sharedCb](const DrogonDbException &e) {
          LOG_WARN << "[PG-Identity] resetFailedLogins FAILED: " << e.base().what();
          (*sharedCb)(false);
      }
    );
}

void PostgresIdentityRepository::incrementFailedLogins(
  int32_t userId,
  std::function<void(bool)> &&callback
)
{
    if (!dbClient_)
    {
        callback(false);
        return;
    }
    auto sharedCb = std::make_shared<std::function<void(bool)>>(std::move(callback));

    Mapper<Users> mapper(dbClient_);
    mapper.findBy(
      Criteria(Users::Cols::_id, CompareOperator::EQ, userId),
      [sharedCb, self = shared_from_this()](const std::vector<Users> &users) {
          int failedCount = users.empty() ? 0 : users[0].getValueOfFailedLoginCount();
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

          if (users.empty())
          {
              (*sharedCb)(false);
              return;
          }
          auto updated = std::make_shared<Users>(users[0]);
          updated->setFailedLoginCount(newFailedCount);
          updated->setLockedUntil(newLockedUntil);
          updated->setLastFailedLogin(now);
          Mapper<Users>(self->dbClient_)
            .update(
              *updated,
              [updated, sharedCb](const size_t) { (*sharedCb)(true); },
              [updated, sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
            );
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
    );
}

void PostgresIdentityRepository::getUserInfoWithRoles(
  int32_t userId,
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
        Mapper<Users>(db).findBy(
          Criteria(Users::Cols::_id, CompareOperator::EQ, userId),
          [sharedCb, db, userId](const std::vector<Users> &users) {
              if (users.empty())
              {
                  (*sharedCb)(std::nullopt);
                  return;
              }
              const auto &user = users[0];

              // Split JOIN: UserRoles::findBy + Roles::findBy for each role
              Mapper<UserRoles>(db).findBy(
                Criteria(UserRoles::Cols::_user_id, CompareOperator::EQ, userId),
                [sharedCb, db, user](const std::vector<UserRoles> &userRoles) {
                    if (userRoles.empty())
                    {
                        Json::Value json;
                        json["sub"] = user.getValueOfPublicSub();
                        std::string dn = user.getValueOfUsername();
                        json["name"] = dn.empty() ? user.getValueOfEmail() : dn;
                        json["email"] = user.getValueOfEmail();
                        json["roles"] = Json::Value(Json::arrayValue);
                        (*sharedCb)(json);
                        return;
                    }

                    std::vector<int32_t> roleIds;
                    for (const auto &ur : userRoles)
                        roleIds.push_back(ur.getValueOfRoleId());

                    Mapper<Roles>(db).findBy(
                      Criteria(Roles::Cols::_id, CompareOperator::In, roleIds),
                      [sharedCb, user](const std::vector<Roles> &roles) {
                          Json::Value json;
                          json["sub"] = user.getValueOfPublicSub();
                          std::string dn = user.getValueOfUsername();
                          json["name"] = dn.empty() ? user.getValueOfEmail() : dn;
                          json["email"] = user.getValueOfEmail();
                          Json::Value rj(Json::arrayValue);
                          for (const auto &r : roles)
                              rj.append(r.getValueOfName());
                          json["roles"] = rj;
                          (*sharedCb)(json);
                      },
                      [sharedCb, user](const DrogonDbException &) {
                          Json::Value json;
                          json["sub"] = user.getValueOfPublicSub();
                          std::string dn = user.getValueOfUsername();
                          json["name"] = dn.empty() ? user.getValueOfEmail() : dn;
                          json["email"] = user.getValueOfEmail();
                          json["roles"] = Json::Value(Json::arrayValue);
                          (*sharedCb)(json);
                      }
                    );
                },
                [sharedCb, user](const DrogonDbException &) {
                    Json::Value json;
                    json["sub"] = user.getValueOfPublicSub();
                    std::string dn = user.getValueOfUsername();
                    json["name"] = dn.empty() ? user.getValueOfEmail() : dn;
                    json["email"] = user.getValueOfEmail();
                    json["roles"] = Json::Value(Json::arrayValue);
                    (*sharedCb)(json);
                }
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
  int32_t internalUserId,
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
          Criteria(UserRoles::Cols::_user_id, CompareOperator::EQ, internalUserId),
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
  std::function<void(std::optional<int32_t>)> &&cb
)
{
    if (!dbClient_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<std::function<void(std::optional<int32_t>)>>(std::move(cb));
    try
    {
        Mapper<Oauth2SubjectMappings> mapper(dbClient_);
        mapper.findOne(
          Criteria(Oauth2SubjectMappings::Cols::_provider, CompareOperator::EQ, provider) &&
            Criteria(Oauth2SubjectMappings::Cols::_subject, CompareOperator::EQ, subject),
          [sharedCb](const Oauth2SubjectMappings &mapping) {
              (*sharedCb)(mapping.getValueOfInternalUserId());
          },
          [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
        );
    }
    catch (...)
    {
        (*sharedCb)(std::nullopt);
    }
}

// Phase 1.5b (Task 39): subject-string overload. Ported from the legacy
// oauth2::PostgresRoleRepository::getUserRoles(string) -- numeric subject is
// used directly as the internal id; otherwise treated as users.public_sub and
// resolved to the internal id first, then the int32 path is reused.
void PostgresIdentityRepository::getRoles(
  const std::string &subject,
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

    // Numeric subject -> internal id directly.
    bool isNumeric = false;
    int32_t numericId = 0;
    try
    {
        size_t pos = 0;
        int parsed = std::stoi(subject, &pos);
        isNumeric = (pos == subject.length());
        if (isNumeric)
            numericId = parsed;
    }
    catch (...)
    {
        isNumeric = false;
    }

    if (isNumeric)
    {
        getRoles(numericId, [sharedCb](std::vector<std::string> roles) { (*sharedCb)(roles); });
        return;
    }

    // Otherwise resolve public_sub -> internal id, then the int32 path.
    try
    {
        Mapper<Users> userMapper(db);
        userMapper.findOne(
          Criteria(Users::Cols::_public_sub, CompareOperator::EQ, subject),
          [this, sharedCb](const Users &user) {
              getRoles(user.getValueOfId(), [sharedCb](std::vector<std::string> roles) {
                  (*sharedCb)(roles);
              });
          },
          [sharedCb](const DrogonDbException &) { (*sharedCb)({}); }
        );
    }
    catch (...)
    {
        (*sharedCb)({});
    }
}

// Phase 1.5b (Task 39): write path, ported from the legacy
// oauth2::PostgresSubjectMappingRepository.
void PostgresIdentityRepository::createSubjectMapping(
  const std::string &subject,
  int32_t internalUserId,
  const std::string &provider,
  std::function<void(bool)> &&cb
)
{
    if (!dbClient_)
    {
        cb(false);
        return;
    }
    auto sharedCb = std::make_shared<std::function<void(bool)>>(std::move(cb));
    try
    {
        Mapper<Oauth2SubjectMappings> mapper(dbClient_);
        Oauth2SubjectMappings mapping;
        mapping.setSubject(subject);
        mapping.setInternalUserId(internalUserId);
        mapping.setProvider(provider);
        mapper.insert(
          mapping,
          [sharedCb](const Oauth2SubjectMappings &) { (*sharedCb)(true); },
          [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
        );
    }
    catch (...)
    {
        (*sharedCb)(false);
    }
}

// Phase 1.5b (Task 39): raw SQL INSERT ... ON CONFLICT ... RETURNING is the
// upsert exemption in .claude/rules/db-operations.md (the Mapper cannot
// express ON CONFLICT). Ported verbatim from the legacy
// oauth2::PostgresSubjectMappingRepository::createUserForExternalLogin.
void PostgresIdentityRepository::createUserForExternalLogin(
  const std::string &externalId,
  const std::string &provider,
  std::function<void(std::optional<int32_t>)> &&cb
)
{
    if (!dbClient_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<std::function<void(std::optional<int32_t>)>>(std::move(cb));

    std::string username = provider + "_" + externalId.substr(0, 20);
    dbClient_->execSqlAsync(
      "INSERT INTO users (username, password_hash, salt, email) "
      "VALUES ($1, 'EXTERNAL_AUTH_NO_PASSWORD', '', '') "
      "ON CONFLICT (username) DO UPDATE SET username = users.username "
      "RETURNING id",
      [sharedCb](const Result &r) {
          if (r.empty())
          {
              (*sharedCb)(std::nullopt);
              return;
          }
          (*sharedCb)(r[0]["id"].as<int32_t>());
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); },
      username
    );
}

}  // namespace authforge::storage::postgres
