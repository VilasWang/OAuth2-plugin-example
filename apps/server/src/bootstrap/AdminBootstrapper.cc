#include "AdminBootstrapper.h"

#include <fulla/storage/postgres/models/Oauth2SubjectMappings.h>
#include <fulla/storage/postgres/models/Roles.h>
#include <fulla/storage/postgres/models/UserRoles.h>
#include <fulla/storage/postgres/models/Users.h>
#include <drogon/drogon.h>
#include <drogon/orm/Mapper.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <memory>
#include <vector>

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::fulla_db;

namespace bootstrap
{
namespace
{
// Same PBKDF2-SHA256 parameters/format as identity AuthService::hashPassword
// ("$pbkdf2-sha256$310000$<hexsalt>$<hexhash>"). Kept local so the
// bootstrapper does not depend on identity service wiring order.
constexpr int kIterations = 310000;
constexpr int kSaltLength = 16;
constexpr int kKeyLength = 32;

std::string bytesToHex(const unsigned char *data, size_t len)
{
    static const char *hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++)
    {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0x0F]);
    }
    return out;
}

std::string hashPasswordPbkdf2(const std::string &password)
{
    unsigned char salt[kSaltLength];
    if (RAND_bytes(salt, kSaltLength) != 1)
        throw std::runtime_error("AdminBootstrapper: RAND_bytes failed");
    unsigned char key[kKeyLength];
    if (
      PKCS5_PBKDF2_HMAC(
        password.data(), static_cast<int>(password.size()), salt, kSaltLength, kIterations,
        EVP_sha256(), kKeyLength, key
      ) != 1
    )
        throw std::runtime_error("AdminBootstrapper: PBKDF2 failed");
    return "$pbkdf2-sha256$" + std::to_string(kIterations) + "$" + bytesToHex(salt, kSaltLength) +
           "$" + bytesToHex(key, kKeyLength);
}

std::string randomPassword()
{
    unsigned char raw[24];
    if (RAND_bytes(raw, sizeof(raw)) != 1)
        throw std::runtime_error("AdminBootstrapper: RAND_bytes failed");
    // Printable, unambiguous alphabet; no dependency on a base64 lib.
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";
    std::string out;
    out.reserve(31);
    for (size_t i = 0; i < sizeof(raw); i++)
    {
        out.push_back(alphabet[raw[i] % 56]);
        if ((i + 1) % 8 == 0 && i + 1 < sizeof(raw))
            out.push_back('-');
    }
    return out;
}

using DonePtr = std::shared_ptr<AdminBootstrapper::DoneCallback>;

void finish(const DonePtr &done, bool created, const std::string &detail)
{
    (*done)(created, detail);
}

void insertSubjectMapping(
  const DbClientPtr &db,
  const DonePtr &done,
  int32_t userId
)
{
    try
    {
        Mapper<Oauth2SubjectMappings> mapMapper(db);
        Oauth2SubjectMappings mapping;
        mapping.setSubject(std::to_string(userId));
        mapping.setInternalUserId(userId);
        mapping.setProvider("local");
        mapMapper.insert(
          mapping,
          [done](const Oauth2SubjectMappings &) { finish(done, true, "admin bootstrapped"); },
          [done](const DrogonDbException &e) {
              // User + role exist; the mapping is recoverable on next login
              // paths — report success with a note.
              LOG_WARN << "AdminBootstrapper: subject mapping insert failed: " << e.base().what();
              finish(done, true, "admin bootstrapped (mapping deferred)");
          }
        );
    }
    catch (const DrogonDbException &e)
    {
        LOG_WARN << "AdminBootstrapper: mapping mapper failed: " << e.base().what();
        finish(done, true, "admin bootstrapped (mapping deferred)");
    }
}

void assignAdminRole(
  const DbClientPtr &db,
  const DonePtr &done,
  int32_t userId,
  int32_t roleId
)
{
    try
    {
        Mapper<UserRoles> urMapper(db);
        UserRoles ur;
        ur.setUserId(userId);
        ur.setRoleId(roleId);
        urMapper.insert(
          ur,
          [db, done, userId](const UserRoles &) { insertSubjectMapping(db, done, userId); },
          [done, userId](const DrogonDbException &e) {
              LOG_WARN << "AdminBootstrapper: role assignment failed: " << e.base().what();
              // User exists; without the role it is not an admin — surface as
              // not-created so the operator sees the failure.
              finish(done, false, "role assignment failed (user 'admin' exists without role)");
              (void)userId;
          }
        );
    }
    catch (const DrogonDbException &e)
    {
        finish(done, false, std::string("user_roles mapper failed: ") + e.base().what());
    }
}

void createAdmin(const DbClientPtr &db, const DonePtr &done, int32_t roleId, const std::string &explicitPassword)
{
    const bool generated = explicitPassword.empty();
    const std::string password = generated ? randomPassword() : explicitPassword;
    std::string passwordHash;
    try
    {
        passwordHash = hashPasswordPbkdf2(password);
    }
    catch (const std::exception &e)
    {
        finish(done, false, e.what());
        return;
    }

    if (generated)
    {
        LOG_WARN << "==========================================================";
        LOG_WARN << "Bootstrap: created administrator 'admin' with password: " << password;
        LOG_WARN << "SAVE IT NOW and change it after first login. It is shown ONCE.";
        LOG_WARN << "==========================================================";
    }
    else
    {
        LOG_INFO << "Bootstrap: created administrator 'admin' (password from env)";
    }

    try
    {
        Mapper<Users> userMapper(db);
        Users admin;
        admin.setUsername("admin");
        admin.setPasswordHash(passwordHash);
        admin.setEmail("admin@example.com");
        userMapper.insert(
          admin,
          [db, done, roleId](const Users &inserted) {
              assignAdminRole(db, done, inserted.getValueOfId(), roleId);
          },
          [done](const DrogonDbException &e) {
              const std::string what = e.base().what();
              if (what.find("users_username_key") != std::string::npos)
                  finish(done, false, "username 'admin' already exists (without admin role)");
              else
                  finish(done, false, "user insert failed: " + what);
          }
        );
    }
    catch (const DrogonDbException &e)
    {
        finish(done, false, std::string("users mapper failed: ") + e.base().what());
    }
}
}  // namespace

void AdminBootstrapper::run(const std::string &explicitPassword, DoneCallback &&done)
{
    auto sharedDone = std::make_shared<DoneCallback>(std::move(done));
    auto db = app().getDbClient();
    if (!db)
    {
        finish(sharedDone, false, "no db client (memory storage?)");
        return;
    }

    try
    {
        Mapper<Roles> roleMapper(db);
        roleMapper.findOne(
          Criteria(Roles::Cols::_name, CompareOperator::EQ, std::string("admin")),
          [db, sharedDone, explicitPassword](const Roles &adminRole) mutable {
              try
              {
                  Mapper<UserRoles> urMapper(db);
                  urMapper.findBy(
                    Criteria(
                      UserRoles::Cols::_role_id, CompareOperator::EQ, adminRole.getValueOfId()
                    ),
                    [db, sharedDone, explicitPassword, roleId = adminRole.getValueOfId()](
                      const std::vector<UserRoles> &rows) mutable {
                        if (!rows.empty())
                        {
                            finish(sharedDone, false, "admin role already assigned; nothing to do");
                            return;
                        }
                        createAdmin(db, sharedDone, roleId, explicitPassword);
                    },
                    [sharedDone](const DrogonDbException &e) {
                        finish(
                          sharedDone, false,
                          std::string("user_roles query failed: ") + e.base().what()
                        );
                    }
                  );
              }
              catch (const DrogonDbException &e)
              {
                  finish(
                    sharedDone, false,
                    std::string("user_roles mapper failed: ") + e.base().what()
                  );
              }
          },
          [sharedDone](const DrogonDbException &e) {
              finish(
                sharedDone, false,
                std::string("role 'admin' not found (roles not seeded?): ") + e.base().what()
              );
          }
        );
    }
    catch (...)
    {
        finish(sharedDone, false, "INTERNAL_ERROR");
    }
}
}  // namespace bootstrap
