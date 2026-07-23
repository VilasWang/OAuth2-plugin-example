#include <authforge/storage/postgres/PostgresSocialAccountRepository.h>

#ifdef WITH_SOCIAL

#include <drogon/drogon.h>

namespace authforge::storage::postgres
{

using namespace ::drogon::orm;
using authforge::identity::LinkNewSocialAccountResult;
using authforge::identity::SocialAccountLookup;

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
    auto db = dbClient_;
    auto sharedCb = std::make_shared<LookupCallback>(std::move(cb));
    db->execSqlAsync(
      "SELECT internal_user_id FROM oauth2_subject_mappings WHERE provider = $1 AND subject = $2",
      [db, sharedCb](const Result &mappingResult) {
          if (mappingResult.empty())
          {
              (*sharedCb)(std::nullopt);
              return;
          }
          int64_t userId = mappingResult[0]["internal_user_id"].as<int64_t>();
          db->execSqlAsync(
            "SELECT username FROM users WHERE id = $1",
            [sharedCb, userId](const Result &r) {
                SocialAccountLookup lookup;
                lookup.userId = userId;
                lookup.username = r.empty() ? "user" : r[0]["username"].as<std::string>();
                (*sharedCb)(lookup);
            },
            [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); },
            static_cast<int32_t>(userId)
          );
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); },
      provider,
      subject
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

    db->execSqlAsync(
      "INSERT INTO users (username, password_hash, salt, email, email_verified) "
      "VALUES ($1, $2, '', $3, true) "
      "ON CONFLICT (username) DO UPDATE SET email = EXCLUDED.email, email_verified = true "
      "RETURNING id",
      [db, sharedCb, provider, subject, username](const Result &userResult) {
          int64_t userId = userResult[0]["id"].as<int64_t>();
          db->execSqlAsync(
            "INSERT INTO oauth2_subject_mappings (subject, internal_user_id, provider) "
            "VALUES ($1, $2, $3) ON CONFLICT DO NOTHING",
            [db, sharedCb, userId, username](const Result &) {
                db->execSqlAsync(
                  "INSERT INTO user_roles (user_id, role_id) "
                  "SELECT $1, id FROM roles WHERE name = 'user' ON CONFLICT DO NOTHING",
                  [sharedCb, userId, username](const Result &) {
                      LinkNewSocialAccountResult result;
                      result.userId = userId;
                      result.username = username;
                      (*sharedCb)(result);
                  },
                  [sharedCb, userId, username](const DrogonDbException &) {
                      // Best-effort role assignment, mirrors
                      // GitHubController.cc's existing tolerance.
                      LinkNewSocialAccountResult result;
                      result.userId = userId;
                      result.username = username;
                      (*sharedCb)(result);
                  },
                  static_cast<int32_t>(userId)
                );
            },
            [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); },
            subject,
            static_cast<int32_t>(userId),
            provider
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
