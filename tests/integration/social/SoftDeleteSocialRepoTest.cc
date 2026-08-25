// tests/integration/social/SoftDeleteSocialRepoTest.cc
//
// Repo-level integration tests (issue #54) for
// PostgresSocialAccountRepository's soft-delete/lock enforcement against a
// real Postgres:
//
//   findLinkedUser:
//     - no mapping                  -> NoMapping
//     - mapping -> live user        -> Linked
//     - mapping -> soft-deleted     -> AccountUnavailable (V024 contract)
//     - mapping -> missing row      -> AccountUnavailable (hard-deleted)
//     - mapping -> locked user      -> AccountUnavailable (parity with the
//                                       password path's lockedUntil check)
//   createLinkedUser:
//     - username held by an ACTIVE row   -> nullopt (ON CONFLICT DO NOTHING:
//                                            no row adoption / takeover)
//     - username held by a SOFT-DELETED  -> nullopt (no resurrection)
//
// The HTTP-level behavior (controller wiring) is covered by
// SocialLoginHttpTest.cc against the Fake repo; these tests hit the REAL
// production repository implementation.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>

#include <fulla/identity/ISocialAccountRepository.h>
#include <fulla/storage/postgres/PostgresSocialAccountRepository.h>

#include "HttpTestClient.h"

#include <chrono>
#include <cstdio>
#include <future>
#include <string>

using fulla::identity::SocialLinkStatus;
using fulla::test::http::postgresAvailable;

namespace
{
// Synchronous exec helper for seeding (tests may block; the DB is fast).
bool execSql(const std::string &sql)
{
    try
    {
        auto db = drogon::app().getDbClient();
        if (!db)
            return false;
        std::promise<bool> p;
        db->execSqlAsync(
          sql,
          [&p](const drogon::orm::Result &) { p.set_value(true); },
          [&p](const drogon::orm::DrogonDbException &e) {
              LOG_ERROR << "SoftDeleteSocialRepoTest seed failed: " << e.base().what();
              p.set_value(false);
          }
        );
        return p.get_future().get();
    }
    catch (...)
    {
        return false;
    }
}

// Insert a throwaway user row; returns its id (-1 on failure). deleted=true
// sets deleted_at (soft-deleted).
int32_t seedUser(const std::string &username, bool deleted, bool locked)
{
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch()
    )
                 .count();
    // email is omitted (NULL): an empty-string email collides on
    // idx_users_email_unique across the test's multiple seeded rows.
    std::string sql =
      "INSERT INTO users (username, password_hash, salt, email_verified, "
      "locked_until) VALUES ('" +
      username + "', 'x', '', false, " + (locked ? std::to_string(now + 3600) : "0") +
      ") RETURNING id";
    if (deleted)
    {
        sql =
          "INSERT INTO users (username, password_hash, salt, email_verified, "
          "locked_until, deleted_at) VALUES ('" +
          username + "', 'x', '', false, 0, NOW()) RETURNING id";
    }
    try
    {
        auto db = drogon::app().getDbClient();
        if (!db)
            return -1;
        std::promise<int32_t> p;
        db->execSqlAsync(
          sql,
          [&p](const drogon::orm::Result &r) {
              p.set_value(r.empty() ? -1 : r[0]["id"].as<int32_t>());
          },
          [&p, sql](const drogon::orm::DrogonDbException &e) {
              // fprintf (not just LOG_ERROR): drogon's log sink may be
              // redirected in the test harness; this must be visible.
              std::fprintf(
                stderr, "seedUser failed: %s\nSQL: %s\n", e.base().what(), sql.c_str()
              );
              LOG_ERROR << "seedUser failed: " << e.base().what();
              p.set_value(-1);
          }
        );
        return p.get_future().get();
    }
    catch (...)
    {
        return -1;
    }
}

bool seedMapping(const std::string &provider, const std::string &subject, int32_t userId)
{
    return execSql(
      "INSERT INTO oauth2_subject_mappings (provider, subject, internal_user_id) "
      "VALUES ('" +
      provider + "', '" + subject + "', " + std::to_string(userId) + ")"
    );
}

// Blocking wrapper: run findLinkedUser on the real repo and return its status.
SocialLinkStatus runFindLinkedUser(
  const std::shared_ptr<fulla::storage::postgres::PostgresSocialAccountRepository> &repo,
  const std::string &provider,
  const std::string &subject
)
{
    std::promise<SocialLinkStatus> p;
    repo->findLinkedUser(
      provider,
      subject,
      [&p](SocialLinkStatus status, const fulla::identity::SocialAccountLookup &) {
          p.set_value(status);
      }
    );
    return p.get_future().get();
}
}  // namespace

DROGON_TEST(Integration_P0_SocialRepo_FindLinkedUser_SoftDeleteEnforcement)
{
    if (!postgresAvailable())
    {
        CHECK(true);
        return;
    }
    auto db = drogon::app().getDbClient();
    REQUIRE(db != nullptr);
    auto repo = std::make_shared<fulla::storage::postgres::PostgresSocialAccountRepository>(db);

    const auto suffix = std::to_string(
      std::chrono::high_resolution_clock::now().time_since_epoch().count() % 1000000
    );

    // No mapping -> NoMapping.
    CHECK(runFindLinkedUser(repo, "github", "nosubject_" + suffix) == SocialLinkStatus::NoMapping);

    // Mapping -> live user -> Linked.
    {
        int32_t uid = seedUser("social_live_" + suffix, false, false);
        REQUIRE(uid > 0);
        REQUIRE(seedMapping("github", "live_" + suffix, uid));
        CHECK(runFindLinkedUser(repo, "github", "live_" + suffix) == SocialLinkStatus::Linked);
    }

    // Mapping -> soft-deleted user -> AccountUnavailable (the #54 bypass).
    {
        int32_t uid = seedUser("social_dead_" + suffix, true, false);
        REQUIRE(uid > 0);
        REQUIRE(seedMapping("github", "dead_" + suffix, uid));
        CHECK(
          runFindLinkedUser(repo, "github", "dead_" + suffix) ==
          SocialLinkStatus::AccountUnavailable
        );
    }

    // Hard-deleted user row: oauth2_subject_mappings.internal_user_id has
    // ON DELETE CASCADE (V006), so the mapping goes away WITH the user — the
    // natural post-hard-delete state is NoMapping (a fresh GitHub login may
    // create a new account). The repo's empty-users-vector -> AccountUn-
    // available branch remains defense-in-depth for corrupted/externally-
    // mutated data and is covered by the Fake at the HTTP layer.
    {
        int32_t uid = seedUser("social_gone_" + suffix, false, false);
        REQUIRE(uid > 0);
        REQUIRE(seedMapping("github", "gone_" + suffix, uid));
        REQUIRE(execSql("DELETE FROM users WHERE id = " + std::to_string(uid)));
        CHECK(
          runFindLinkedUser(repo, "github", "gone_" + suffix) == SocialLinkStatus::NoMapping
        );
    }

    // Mapping -> locked user -> AccountUnavailable (disable must mean cannot
    // log in through ANY flow).
    {
        int32_t uid = seedUser("social_locked_" + suffix, false, true);
        REQUIRE(uid > 0);
        REQUIRE(seedMapping("github", "locked_" + suffix, uid));
        CHECK(
          runFindLinkedUser(repo, "github", "locked_" + suffix) ==
          SocialLinkStatus::AccountUnavailable
        );
    }
}

DROGON_TEST(Integration_P0_SocialRepo_CreateLinkedUser_FailClosedOnConflict)
{
    if (!postgresAvailable())
    {
        CHECK(true);
        return;
    }
    auto db = drogon::app().getDbClient();
    REQUIRE(db != nullptr);
    auto repo = std::make_shared<fulla::storage::postgres::PostgresSocialAccountRepository>(db);

    const auto suffix = std::to_string(
      std::chrono::high_resolution_clock::now().time_since_epoch().count() % 1000000
    );

    // Happy path (PR-review finding 1): a created linked account must
    // actually HAVE the default 'user' role — the grant previously inserted
    // user_roles without role_id (NOT NULL violation, silently swallowed).
    {
        const std::string uname = "gh_rolecheck_" + suffix;
        std::promise<std::optional<fulla::identity::LinkNewSocialAccountResult>> p;
        repo->createLinkedUser(
          "github", "rolecheck_" + suffix, uname, "x@example.com",
          [&p](std::optional<fulla::identity::LinkNewSocialAccountResult> r) {
              p.set_value(r);
          }
        );
        auto created = p.get_future().get();
        REQUIRE(created.has_value());
        // Verify the user_roles row exists and points at the real 'user'
        // role (two split queries — JOIN-forbidden even in seed checks).
        std::promise<int32_t> cnt;
        db->execSqlAsync(
          "SELECT COUNT(*) AS n FROM user_roles WHERE user_id = $1 AND role_id = "
          "(SELECT id FROM roles WHERE name = 'user')",
          [&cnt](const drogon::orm::Result &res) {
              cnt.set_value(res.empty() ? -1 : res[0]["n"].as<int32_t>());
          },
          [&cnt](const drogon::orm::DrogonDbException &e) {
              LOG_ERROR << "role-count check failed: " << e.base().what();
              cnt.set_value(-1);
          },
          created->userId
        );
        CHECK(cnt.get_future().get() == 1);
    }

    // Username held by an ACTIVE row: DO NOTHING -> nullopt, and the existing
    // row is untouched (no adoption / takeover).
    {
        std::string held = "gh_holdme_" + suffix;
        int32_t uid = seedUser(held, false, false);
        REQUIRE(uid > 0);

        std::promise<bool> p;
        repo->createLinkedUser(
          "github", "conflict1_" + suffix, held, "x@example.com",
          [&p](std::optional<fulla::identity::LinkNewSocialAccountResult> r) {
              p.set_value(!r.has_value());
          }
        );
        CHECK(p.get_future().get());
        // No mapping may have been created for the attacker's subject.
        auto status = runFindLinkedUser(repo, "github", "conflict1_" + suffix);
        CHECK(status == SocialLinkStatus::NoMapping);
    }

    // Username held by a SOFT-DELETED row: same fail-closed refusal (no
    // resurrection of deleted accounts).
    {
        std::string held = "gh_holddel_" + suffix;
        int32_t uid = seedUser(held, true, false);
        REQUIRE(uid > 0);

        std::promise<bool> p;
        repo->createLinkedUser(
          "github", "conflict2_" + suffix, held, "x@example.com",
          [&p](std::optional<fulla::identity::LinkNewSocialAccountResult> r) {
              p.set_value(!r.has_value());
          }
        );
        CHECK(p.get_future().get());
    }
}

// ---------------------------------------------------------------------------
// B2 link/unlink lifecycle against the REAL repository (PR review S1):
// listForUser (local-row exclusion + ISO-8601 linked_at), insertLink
// (Inserted + the UNIQUE-violation Conflict path), deleteLink (Deleted /
// NoLink), userHasUsablePassword (non-pbkdf2 hash -> false).
// ---------------------------------------------------------------------------
DROGON_TEST(Integration_P0_SocialRepo_LinkLifecycle_ListInsertDelete)
{
    if (!postgresAvailable())
    {
        CHECK(true);
        return;
    }
    auto db = drogon::app().getDbClient();
    REQUIRE(db != nullptr);
    auto repo = std::make_shared<fulla::storage::postgres::PostgresSocialAccountRepository>(db);

    const auto suffix = std::to_string(
      std::chrono::high_resolution_clock::now().time_since_epoch().count() % 1000000
    );

    int32_t uid = seedUser("social_list_" + suffix, false, false);
    REQUIRE(uid > 0);
    int32_t uid2 = seedUser("social_list2_" + suffix, false, false);
    REQUIRE(uid2 > 0);
    // A 'local' mapping (seed/password-flow shape) must NOT appear in the
    // social list nor count toward the last-credential guard.
    REQUIRE(seedMapping("local", "localsub_" + suffix, uid));
    REQUIRE(seedMapping("github", "lifecycle_" + suffix, uid));

    // listForUser: only the social row; timestamp is ISO-8601 with 'T'.
    {
        std::promise<std::optional<std::vector<fulla::identity::SocialLinkEntry>>> p;
        repo->listForUser(
          uid, [&](std::optional<std::vector<fulla::identity::SocialLinkEntry>> e) {
              p.set_value(std::move(e));
          }
        );
        auto entries = p.get_future().get();
        REQUIRE(entries.has_value());
        REQUIRE(entries->size() == 1u);
        CHECK((*entries)[0].provider == "github");
        CHECK((*entries)[0].subject == "lifecycle_" + suffix);
        CHECK((*entries)[0].linkedAt.find('T') != std::string::npos);
    }

    // userHasUsablePassword: seedUser writes password_hash 'x' (not a
    // $pbkdf2-sha256$ PasswordHasher output) -> false, the guard's input
    // for social-created accounts.
    {
        std::promise<std::optional<bool>> p;
        repo->userHasUsablePassword(uid, [&](std::optional<bool> v) { p.set_value(v); });
        auto usable = p.get_future().get();
        REQUIRE(usable.has_value());
        CHECK(*usable == false);
    }

    // insertLink: the (github, lifecycle_) subject already belongs to uid ->
    // claiming it for uid2 hits UNIQUE(provider, subject) -> Conflict (the
    // SQLSTATE/substring detection path).
    {
        std::promise<fulla::identity::LinkMutationStatus> p;
        repo->insertLink(
          "github", "lifecycle_" + suffix, uid2,
          [&p](fulla::identity::LinkMutationStatus s) { p.set_value(s); }
        );
        CHECK(p.get_future().get() == fulla::identity::LinkMutationStatus::Conflict);
    }

    // insertLink happy path for a free subject.
    {
        std::promise<fulla::identity::LinkMutationStatus> p;
        repo->insertLink(
          "google", "gfree_" + suffix, uid,
          [&p](fulla::identity::LinkMutationStatus s) { p.set_value(s); }
        );
        CHECK(p.get_future().get() == fulla::identity::LinkMutationStatus::Inserted);
    }

    // deleteLink: removes exactly the (provider, user) row; second call -> NoLink.
    {
        std::promise<fulla::identity::LinkMutationStatus> p;
        repo->deleteLink(
          "github", uid, [&p](fulla::identity::LinkMutationStatus s) { p.set_value(s); }
        );
        CHECK(p.get_future().get() == fulla::identity::LinkMutationStatus::Deleted);
        std::promise<fulla::identity::LinkMutationStatus> p2;
        repo->deleteLink(
          "github", uid, [&p2](fulla::identity::LinkMutationStatus s) { p2.set_value(s); }
        );
        CHECK(p2.get_future().get() == fulla::identity::LinkMutationStatus::NoLink);
    }
}
