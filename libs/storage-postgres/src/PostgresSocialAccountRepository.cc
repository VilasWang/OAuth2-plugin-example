#include <authforge/storage/postgres/PostgresSocialAccountRepository.h>

#ifdef WITH_SOCIAL

#include <drogon/drogon.h>

#include <authforge/storage/postgres/models/Oauth2SubjectMappings.h>
#include <authforge/storage/postgres/models/UserRoles.h>
#include <authforge/storage/postgres/models/Users.h>

namespace authforge::storage::postgres
{

using namespace ::drogon::orm;
using authforge::identity::LinkNewSocialAccountResult;
using authforge::identity::SocialAccountLookup;
using drogon_model::oauth2_db::Oauth2SubjectMappings;
using drogon_model::oauth2_db::UserRoles;
using drogon_model::oauth2_db::Users;

void PostgresSocialAccountRepository::findLinkedUser(
  const std::string &provider,
  const std::string &subject,
  LookupCallback &&cb
)
{
    if (!dbClient_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<LookupCallback>(std::move(cb));

    Mapper<Oauth2SubjectMappings> mapper(dbClient_);
    mapper.findOne(
      Criteria(
        Oauth2SubjectMappings::Cols::_provider, CompareOperator::EQ, provider
      ) &&
        Criteria(
          Oauth2SubjectMappings::Cols::_subject, CompareOperator::EQ, subject
        ),
      [sharedCb, self = shared_from_this()](const Oauth2SubjectMappings &mapping) {
          int32_t userId32 = mapping.getValueOfInternalUserId();

          Mapper<Users> userMapper(self->dbClient_);
          userMapper.findOne(
            Criteria(Users::Cols::_id, CompareOperator::EQ, userId32),
            [sharedCb, userId32](const Users &user) {
                SocialAccountLookup lookup;
                lookup.userId = userId32;
                lookup.username = user.getValueOfUsername();
                (*sharedCb)(lookup);
            },
            [sharedCb, userId32](const DrogonDbException &) {
                SocialAccountLookup lookup;
                lookup.userId = userId32;
                lookup.username = "user";
                (*sharedCb)(lookup);
            }
          );
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
    );
}

void PostgresSocialAccountRepository::createLinkedUser(
  const std::string &provider,
  const std::string &subject,
  const std::string &username,
  const std::string &email,
  CreateCallback &&cb
)
{
    if (!dbClient_)
    {
        cb(std::nullopt);
        return;
    }
    auto db = dbClient_;
    auto sharedCb = std::make_shared<CreateCallback>(std::move(cb));

    // random password hash -- the account can never log in with a
    // password, mirrors GitHubController.cc's existing
    // authforge::drogon::utils::generateSecureToken() placeholder.
    std::string passwordHash;
    {
        unsigned char buf[32];
        for (auto &b : buf)
            b = static_cast<unsigned char>(rand() % 256);
        static const char hexChars[] = "0123456789abcdef";
        passwordHash.reserve(64);
        for (auto b : buf)
        {
            passwordHash.push_back(hexChars[b >> 4]);
            passwordHash.push_back(hexChars[b & 0x0F]);
        }
    }

    // Exemption (db-operations.md §3): INSERT...RETURNING to capture the
    // auto-generated user id needed by the subsequent subject-mapping and
    // role-assignment inserts.  ON CONFLICT DO UPDATE + RETURNING cannot
    // be expressed via Mapper<T>::insert.
    db->execSqlAsync(
      "INSERT INTO users (username, password_hash, salt, email, email_verified) "
      "VALUES ($1, $2, '', $3, true) "
      "ON CONFLICT (username) DO UPDATE SET email = EXCLUDED.email, email_verified = true "
      "RETURNING id",
      [db, sharedCb, provider, subject, username](const Result &userResult) {
          int32_t userId32 = userResult[0]["id"].as<int32_t>();

          Oauth2SubjectMappings mapping;
          mapping.setSubject(subject);
          mapping.setInternalUserId(userId32);
          mapping.setProvider(provider);

          Mapper<Oauth2SubjectMappings> mapMapper(db);
          mapMapper.insert(
            mapping,
            [db, sharedCb, userId32, username](const Oauth2SubjectMappings &) {
                UserRoles ur;
                ur.setUserId(userId32);

                Mapper<UserRoles> urMapper(db);
                urMapper.insert(
                  ur,
                  [sharedCb, userId32, username](const UserRoles &) {
                      LinkNewSocialAccountResult result;
                      result.userId = userId32;
                      result.username = username;
                      (*sharedCb)(result);
                  },
                  [sharedCb, userId32, username](const DrogonDbException &) {
                      LinkNewSocialAccountResult result;
                      result.userId = userId32;
                      result.username = username;
                      (*sharedCb)(result);
                  }
                );
            },
            [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
          );
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); },
      username,
      passwordHash,
      email
    );
}

}  // namespace authforge::storage::postgres

#endif  // WITH_SOCIAL
