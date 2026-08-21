#include <authforge/storage/postgres/PostgresSocialAccountRepository.h>

#ifdef WITH_SOCIAL

#include <drogon/drogon.h>

#include <chrono>

#include <authforge/storage/postgres/models/Oauth2SubjectMappings.h>
#include <authforge/storage/postgres/models/Roles.h>
#include <authforge/storage/postgres/models/UserRoles.h>
#include <authforge/storage/postgres/models/Users.h>

namespace authforge::storage::postgres
{

using namespace ::drogon::orm;
using authforge::identity::LinkMutationStatus;
using authforge::identity::LinkNewSocialAccountResult;
using authforge::identity::SocialAccountLookup;
using authforge::identity::SocialLinkEntry;
using authforge::identity::SocialLinkStatus;
using drogon_model::oauth2_db::Oauth2SubjectMappings;
using drogon_model::oauth2_db::Roles;
using drogon_model::oauth2_db::UserRoles;
using drogon_model::oauth2_db::Users;

namespace
{
// Current epoch seconds (locked_until is stored as epoch seconds).
int64_t nowEpochSeconds()
{
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch()
    )
                                  .count());
}
}  // namespace

void PostgresSocialAccountRepository::findLinkedUser(
  const std::string &provider,
  const std::string &subject,
  LookupCallback &&cb
)
{
    if (!dbClient_)
    {
        cb(SocialLinkStatus::RepositoryError, SocialAccountLookup{});
        return;
    }
    auto sharedCb = std::make_shared<LookupCallback>(std::move(cb));

    // Both lookups use findBy (vector), not findOne: findOne's error callback
    // fires for BOTH "zero rows" and "DB exception", which would conflate
    // NoMapping with RepositoryError. With findBy the empty vector is a
    // success result and exceptions stay on the error path.
    try
    {
        Mapper<Oauth2SubjectMappings> mapper(dbClient_);
        mapper.findBy(
          Criteria(Oauth2SubjectMappings::Cols::_provider, CompareOperator::EQ, provider) &&
              Criteria(Oauth2SubjectMappings::Cols::_subject, CompareOperator::EQ, subject),
          [self = shared_from_this(), sharedCb](const std::vector<Oauth2SubjectMappings> &mappings) {
              if (mappings.empty())
              {
                  (*sharedCb)(SocialLinkStatus::NoMapping, SocialAccountLookup{});
                  return;
              }
              int32_t userId32 = mappings[0].getValueOfInternalUserId();

              // #54 (V024 soft-delete contract): resolve the mapping to a
              // LIVE user. deleted_at IS NULL excludes soft-deleted users —
              // a deleted user's mapping is intentionally KEPT (soft-delete
              // is reversible), so the mapping resolving to a dead row means
              // "reject the login", never "create a new account".
              try
              {
                  Mapper<Users> userMapper(self->dbClient_);
                  userMapper.findBy(
                    Criteria(Users::Cols::_id, CompareOperator::EQ, userId32) &&
                      Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull),
                    [sharedCb, userId32](const std::vector<Users> &users) {
                        if (users.empty())
                        {
                            // Mapping exists but the user row is gone (hard
                            // delete) or soft-deleted.
                            (*sharedCb)(SocialLinkStatus::AccountUnavailable, SocialAccountLookup{});
                            return;
                        }
                        // Locked users are rejected too — parity with the
                        // password path (AuthService.cc checks lockedUntil
                        // before issuing tokens), so "disabled" means cannot
                        // log in through ANY flow.
                        if (users[0].getValueOfLockedUntil() > nowEpochSeconds())
                        {
                            (*sharedCb)(SocialLinkStatus::AccountUnavailable, SocialAccountLookup{});
                            return;
                        }
                        SocialAccountLookup lookup;
                        lookup.userId = userId32;
                        lookup.username = users[0].getValueOfUsername();
                        (*sharedCb)(SocialLinkStatus::Linked, lookup);
                    },
                    [sharedCb](const DrogonDbException &e) {
                        LOG_ERROR << "findLinkedUser: user liveness query failed: "
                                  << e.base().what();
                        // The old code FAKED a success (username "user") here,
                        // issuing tokens for a user it could not even read.
                        (*sharedCb)(SocialLinkStatus::RepositoryError, SocialAccountLookup{});
                    }
                  );
              }
              catch (...)
              {
                  LOG_ERROR << "findLinkedUser: users Mapper construction failed";
                  (*sharedCb)(SocialLinkStatus::RepositoryError, SocialAccountLookup{});
              }
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "findLinkedUser: mapping query failed: " << e.base().what();
              (*sharedCb)(SocialLinkStatus::RepositoryError, SocialAccountLookup{});
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "findLinkedUser: mappings Mapper construction failed";
        (*sharedCb)(SocialLinkStatus::RepositoryError, SocialAccountLookup{});
    }
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
    // role-assignment inserts.  ON CONFLICT DO NOTHING + RETURNING cannot
    // be expressed via Mapper<T>::insert.
    //
    // #54 security note: DO NOTHING (fail-closed), NOT the previous
    // DO UPDATE ... RETURNING. The old upsert "adopted" whatever row already
    // held the username — including a soft-DELETED row (resurrecting it with
    // a fresh mapping→dead-user link) and, worse, an ACTIVE row registered
    // by someone else: a victim who locally registered "gh_alice" would be
    // silently taken over by a GitHub user named "alice" (the upsert
    // returned the victim's id and created attacker_subject→victim_id). A
    // username conflict now yields an EMPTY result and the login fails.
    db->execSqlAsync(
      "INSERT INTO users (username, password_hash, salt, email, email_verified) "
      "VALUES ($1, $2, '', $3, true) "
      "ON CONFLICT (username) DO NOTHING "
      "RETURNING id",
      [db, sharedCb, provider, subject, username](const Result &userResult) {
          // DO NOTHING on conflict → no row returned. Check BEFORE indexing:
          // Result::operator[](0) on an empty Result is UB, not an exception.
          if (userResult.empty())
          {
              LOG_ERROR << "createLinkedUser: username '" << username
                        << "' conflicts with an existing row (ON CONFLICT DO NOTHING "
                           "— refusing to adopt it)";
              (*sharedCb)(std::nullopt);
              return;
          }
          int32_t userId32 = userResult[0]["id"].as<int32_t>();

          Oauth2SubjectMappings mapping;
          mapping.setSubject(subject);
          mapping.setInternalUserId(userId32);
          mapping.setProvider(provider);

          try
          {
              Mapper<Oauth2SubjectMappings> mapMapper(db);
              mapMapper.insert(
                mapping,
                [db, sharedCb, userId32, username](const Oauth2SubjectMappings &) {
                    // Default-role grant. PR-review finding 1: this insert
                    // previously set ONLY user_id — role_id is NOT NULL with
                    // no default, so every grant raised a constraint
                    // violation that the best-effort callbacks swallowed
                    // silently: every social account was created with ZERO
                    // roles. Resolve the 'user' role id first (split query,
                    // JOIN-forbidden), then insert with both columns.
                    auto finish = [sharedCb, userId32, username]() {
                        LinkNewSocialAccountResult result;
                        result.userId = userId32;
                        result.username = username;
                        (*sharedCb)(result);
                    };
                    auto grantRole = [db, sharedCb, userId32, username,
                                      finish = std::move(finish)](int32_t roleId) {
                        UserRoles ur;
                        ur.setUserId(userId32);
                        ur.setRoleId(roleId);
                        try
                        {
                            Mapper<UserRoles> urMapper(db);
                            urMapper.insert(
                              ur,
                              [finish](const UserRoles &) { finish(); },
                              [finish](const DrogonDbException &e) {
                                  // Best-effort per the interface contract, but
                                  // LOUD: a permission-less account is exactly
                                  // the silent failure class issue #60-1 fixed.
                                  LOG_ERROR << "createLinkedUser: default-role grant failed: "
                                            << e.base().what();
                                  finish();
                              }
                            );
                        }
                        catch (...)
                        {
                            LOG_ERROR << "createLinkedUser: user-roles Mapper construction failed";
                            finish();
                        }
                    };
                    try
                    {
                        Mapper<Roles> rolesMapper(db);
                        rolesMapper.findOne(
                          Criteria(Roles::Cols::_name, CompareOperator::EQ, std::string("user")),
                          [grantRole = std::move(grantRole)](const Roles &r) {
                              grantRole(r.getValueOfId());
                          },
                          [finish](const DrogonDbException &e) {
                              LOG_ERROR << "createLinkedUser: 'user' role lookup failed: "
                                        << e.base().what();
                              finish();
                          }
                        );
                    }
                    catch (...)
                    {
                        LOG_ERROR << "createLinkedUser: roles Mapper construction failed";
                        LinkNewSocialAccountResult result;
                        result.userId = userId32;
                        result.username = username;
                        (*sharedCb)(result);
                    }
                },
                [sharedCb](const DrogonDbException &e) {
                    LOG_ERROR << "createLinkedUser: subject-mapping insert failed: "
                              << e.base().what();
                    (*sharedCb)(std::nullopt);
                }
              );
          }
          catch (...)
          {
              LOG_ERROR << "createLinkedUser: subject-mappings Mapper construction failed";
              (*sharedCb)(std::nullopt);
          }
      },
      [sharedCb](const DrogonDbException &e) {
          LOG_ERROR << "createLinkedUser: user upsert failed: " << e.base().what();
          (*sharedCb)(std::nullopt);
      },
      username,
      passwordHash,
      email
    );
}

// ---------------------------------------------------------------------------
// B2 social link/unlink: mapping-lifecycle operations for an EXISTING local
// user (see ISocialAccountRepository.h's block comment). All Mapper+Criteria,
// no JOIN, no raw SQL.
// ---------------------------------------------------------------------------

void PostgresSocialAccountRepository::listForUser(
  int32_t internalUserId,
  LinkEntriesCallback &&cb
)
{
    if (!dbClient_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<LinkEntriesCallback>(std::move(cb));
    try
    {
        // provider != 'local': the seed/password flow plants a 'local'
        // subject mapping per user (V006's default) that is NOT a social
        // identity -- it must not show up in the social-links list nor count
        // toward the last-credential guard.
        Mapper<Oauth2SubjectMappings>(dbClient_).findBy(
          Criteria(
            Oauth2SubjectMappings::Cols::_internal_user_id, CompareOperator::EQ, internalUserId
          ) &&
              Criteria(
                Oauth2SubjectMappings::Cols::_provider, CompareOperator::NE, std::string("local")
              ),
          [sharedCb](const std::vector<Oauth2SubjectMappings> &mappings) {
              std::vector<SocialLinkEntry> entries;
              entries.reserve(mappings.size());
              for (const auto &m : mappings)
              {
                  SocialLinkEntry e;
                  e.provider = m.getValueOfProvider();
                  e.subject = m.getValueOfSubject();
                  e.linkedAt = m.getValueOfCreatedAt().toDbStringLocal();
                  entries.push_back(std::move(e));
              }
              (*sharedCb)(std::move(entries));
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "listForUser: mapping query failed: " << e.base().what();
              (*sharedCb)(std::nullopt);
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "listForUser: Mapper construction failed";
        (*sharedCb)(std::nullopt);
    }
}

void PostgresSocialAccountRepository::insertLink(
  const std::string &provider,
  const std::string &subject,
  int32_t internalUserId,
  LinkMutationCallback &&cb
)
{
    if (!dbClient_)
    {
        cb(LinkMutationStatus::Error);
        return;
    }
    auto sharedCb = std::make_shared<LinkMutationCallback>(std::move(cb));
    Oauth2SubjectMappings mapping;
    mapping.setProvider(provider);
    mapping.setSubject(subject);
    mapping.setInternalUserId(internalUserId);
    try
    {
        Mapper<Oauth2SubjectMappings>(dbClient_).insert(
          mapping,
          [sharedCb](const Oauth2SubjectMappings &) {
              (*sharedCb)(LinkMutationStatus::Inserted);
          },
          [sharedCb](const DrogonDbException &e) {
              // UNIQUE(provider, subject) race: another user claimed the same
              // provider account between the service's pre-check and this
              // insert. libpq's what() carries no SQLSTATE, so match the
              // human-readable text (same precedent as
              // PostgresConsentRepository.cc's "duplicate key" check).
              const std::string what = e.base().what();
              if (what.find("duplicate key") != std::string::npos)
              {
                  (*sharedCb)(LinkMutationStatus::Conflict);
                  return;
              }
              LOG_ERROR << "insertLink: mapping insert failed: " << what;
              (*sharedCb)(LinkMutationStatus::Error);
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "insertLink: Mapper construction failed";
        (*sharedCb)(LinkMutationStatus::Error);
    }
}

void PostgresSocialAccountRepository::deleteLink(
  const std::string &provider,
  int32_t internalUserId,
  LinkMutationCallback &&cb
)
{
    if (!dbClient_)
    {
        cb(LinkMutationStatus::Error);
        return;
    }
    auto sharedCb = std::make_shared<LinkMutationCallback>(std::move(cb));
    try
    {
        Mapper<Oauth2SubjectMappings>(dbClient_).deleteBy(
          Criteria(Oauth2SubjectMappings::Cols::_provider, CompareOperator::EQ, provider) &&
            Criteria(
              Oauth2SubjectMappings::Cols::_internal_user_id, CompareOperator::EQ, internalUserId
            ),
          [sharedCb](const size_t deleted) {
              (*sharedCb)(deleted > 0 ? LinkMutationStatus::Deleted : LinkMutationStatus::NoLink);
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "deleteLink: mapping delete failed: " << e.base().what();
              (*sharedCb)(LinkMutationStatus::Error);
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "deleteLink: Mapper construction failed";
        (*sharedCb)(LinkMutationStatus::Error);
    }
}

void PostgresSocialAccountRepository::userHasUsablePassword(
  int32_t internalUserId,
  PasswordUsableCallback &&cb
)
{
    if (!dbClient_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<PasswordUsableCallback>(std::move(cb));
    try
    {
        // PasswordHasher is the only legitimate writer of usable password
        // hashes and always emits `$pbkdf2-sha256$...`; social-created
        // accounts carry a random hex placeholder and deleteAccount writes
        // "DELETED". Missing/soft-deleted row -> empty result -> false
        // (fail-safe for the last-credential guard). V024: deleted_at IS
        // NULL.
        Mapper<Users>(dbClient_).findBy(
          Criteria(Users::Cols::_id, CompareOperator::EQ, internalUserId) &&
              Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull) &&
              Criteria(
                Users::Cols::_password_hash, CompareOperator::Like,
                std::string("$pbkdf2-sha256$%")
              ),
          [sharedCb](const std::vector<Users> &users) {
              (*sharedCb)(!users.empty());
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "userHasUsablePassword: user query failed: " << e.base().what();
              (*sharedCb)(std::nullopt);
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "userHasUsablePassword: Mapper construction failed";
        (*sharedCb)(std::nullopt);
    }
}

}  // namespace authforge::storage::postgres

#endif  // WITH_SOCIAL
