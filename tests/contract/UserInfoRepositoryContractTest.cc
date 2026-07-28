// tests/contract/UserInfoRepositoryContractTest.cc
//
// Spec: authforge-sdk-refactor -- 遗留事项 L2 (tasks.md): the dead
// tests/services/AuthServiceGetUserInfoTest.cc targeted the legacy
// authforge::drogon::services::AuthService::getUserInfo, which has NO
// remaining production caller (the live /oauth2/userinfo chain is
// TokenEndpointController -> OAuth2Plugin::getUserInfo ->
// authforge::identity::IUserRepository), and its `sub == numeric id`
// assertion conflicts with the V007 public_sub UUID model. Its genuinely
// uncovered behavior points -- role aggregation, name fallback (username
// empty -> email), empty roles array, not-found nullopt -- live in
// IUserRepository::getUserInfoWithRoles, so this file re-homes that
// coverage as contract tests over the two concrete implementations
// (Postgres/Memory), replacing the dead file.
//
// Fixture note (Postgres): users/roles/user_roles rows are seeded per run
// with collision-safe identifiers (uniqueSuffix) and deleted afterwards;
// user_roles rows also disappear via ON DELETE CASCADE (V005) when the
// user row is deleted, mirroring ConsentRepositoryContractTest.cc.
//
// Deliberate backend divergence (documented, not unified): Memory's
// getUserInfoWithRoles NEVER returns nullopt -- it synthesizes
// user_<id>/user_<id>@example.com claims and defaults roles to {"user"}
// (legacy memory-mode behavior) -- so the not-found/nullopt tier is
// Postgres-only here.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include <authforge/storage/postgres/PostgresIdentityRepository.h>
#include <authforge/storage/memory/MemoryIdentityRepository.h>

#include "ContractFixtures.h"

#include <memory>
#include <optional>
#include <string>

using namespace authforge::test::contract;
using authforge::storage::memory::MemoryIdentityRepository;
using authforge::storage::postgres::PostgresIdentityRepository;

namespace
{

struct SeededUser
{
    int32_t id = -1;
    std::string publicSub;
};

// INSERT a throwaway users row; nullopt username -> SQL NULL username
// (allowed by the email-first model, see RegistrationWithoutUsernameTest);
// public_sub is filled by its gen_random_uuid() default (V007).
SeededUser seedUser(
  const ::drogon::orm::DbClientPtr &db,
  const std::optional<std::string> &username,
  const std::string &email
)
{
    return waitForValue<SeededUser>([&](auto cb) {
        auto onResult = [cb](const ::drogon::orm::Result &r) {
            SeededUser u;
            if (!r.empty())
            {
                u.id = r[0]["id"].as<int32_t>();
                u.publicSub = r[0]["public_sub"].as<std::string>();
            }
            cb(u);
        };
        auto onError = [cb](const ::drogon::orm::DrogonDbException &) { cb(SeededUser{}); };
        if (username)
        {
            db->execSqlAsync(
              "INSERT INTO users (username, password_hash, salt, email) "
              "VALUES ($1, $2, $3, $4) RETURNING id, public_sub::text AS public_sub",
              onResult,
              onError,
              *username,
              std::string("contract-test-hash"),
              std::string("contract-test-salt"),
              email
            );
        }
        else
        {
            db->execSqlAsync(
              "INSERT INTO users (password_hash, salt, email) "
              "VALUES ($1, $2, $3) RETURNING id, public_sub::text AS public_sub",
              onResult,
              onError,
              std::string("contract-test-hash"),
              std::string("contract-test-salt"),
              email
            );
        }
    });
}

int32_t seedRole(const ::drogon::orm::DbClientPtr &db, const std::string &roleName)
{
    return waitForValue<int32_t>([&](auto cb) {
        db->execSqlAsync(
          "INSERT INTO roles (name) VALUES ($1) RETURNING id",
          [cb](const ::drogon::orm::Result &r) { cb(r.empty() ? -1 : r[0]["id"].as<int32_t>()); },
          [cb](const ::drogon::orm::DrogonDbException &) { cb(-1); },
          roleName
        );
    });
}

bool linkUserRole(const ::drogon::orm::DbClientPtr &db, int32_t userId, int32_t roleId)
{
    return waitForValue<bool>([&](auto cb) {
        db->execSqlAsync(
          "INSERT INTO user_roles (user_id, role_id) VALUES ($1, $2)",
          [cb](const ::drogon::orm::Result &) { cb(true); },
          [cb](const ::drogon::orm::DrogonDbException &) { cb(false); },
          userId,
          roleId
        );
    });
}

// Deleting the user cascades the user_roles link (V005 ON DELETE CASCADE).
void cleanupUser(const ::drogon::orm::DbClientPtr &db, int32_t userId)
{
    waitForValue<bool>([&](auto cb) {
        db->execSqlAsync(
          "DELETE FROM users WHERE id = $1",
          [cb](const ::drogon::orm::Result &) { cb(true); },
          [cb](const ::drogon::orm::DrogonDbException &) { cb(false); },
          userId
        );
    });
}

void cleanupRole(const ::drogon::orm::DbClientPtr &db, int32_t roleId)
{
    waitForValue<bool>([&](auto cb) {
        db->execSqlAsync(
          "DELETE FROM roles WHERE id = $1",
          [cb](const ::drogon::orm::Result &) { cb(true); },
          [cb](const ::drogon::orm::DrogonDbException &) { cb(false); },
          roleId
        );
    });
}

std::optional<Json::Value> fetchUserInfo(
  const std::shared_ptr<PostgresIdentityRepository> &repo,
  int32_t userId
)
{
    return waitForValue<std::optional<Json::Value>>([&](auto cb) {
        repo->getUserInfoWithRoles(userId, std::move(cb));
    });
}

}  // namespace

// ===========================================================================
// Postgres
// ===========================================================================

DROGON_TEST(Integration_P0_Contract_Functional_UserInfoRepository_Postgres_UserWithRoles_ReturnsClaimsAndRoles)
{
    auto db = getPostgresClientOrNull();
    if (!db)
        return;

    const std::string suffix = uniqueSuffix();
    const std::string username = "contract_userinfo_" + suffix;
    const std::string email = username + "@example.com";
    const std::string roleName = "contract_userinfo_role_" + suffix;

    auto user = seedUser(db, username, email);
    REQUIRE(user.id > 0);
    REQUIRE(user.publicSub.empty() == false);
    int32_t roleId = seedRole(db, roleName);
    REQUIRE(roleId > 0);
    REQUIRE(linkUserRole(db, user.id, roleId) == true);

    auto repo = std::make_shared<PostgresIdentityRepository>(db);
    auto info = fetchUserInfo(repo, user.id);

    REQUIRE(info.has_value() == true);
    // sub is the public_sub UUID (V007), NOT the numeric internal id --
    // this is exactly the assertion the dead legacy test got wrong.
    CHECK((*info)["sub"].asString() == user.publicSub);
    CHECK((*info)["name"].asString() == username);
    CHECK((*info)["email"].asString() == email);
    REQUIRE((*info)["roles"].isArray() == true);
    REQUIRE((*info)["roles"].size() == 1u);
    CHECK((*info)["roles"][0].asString() == roleName);

    cleanupUser(db, user.id);
    cleanupRole(db, roleId);
}

DROGON_TEST(Integration_P0_Contract_Functional_UserInfoRepository_Postgres_NoRoles_EmptyRolesArray_NameFallsBackToEmail)
{
    auto db = getPostgresClientOrNull();
    if (!db)
        return;

    const std::string suffix = uniqueSuffix();
    const std::string email = "contract_userinfo_" + suffix + "@example.com";

    // NULL username: the name claim must fall back to the email.
    auto user = seedUser(db, std::nullopt, email);
    REQUIRE(user.id > 0);

    auto repo = std::make_shared<PostgresIdentityRepository>(db);
    auto info = fetchUserInfo(repo, user.id);

    REQUIRE(info.has_value() == true);
    CHECK((*info)["sub"].asString() == user.publicSub);
    CHECK((*info)["name"].asString() == email);
    CHECK((*info)["email"].asString() == email);
    // No user_roles rows -> roles present but EMPTY (graceful degradation
    // branch: user info survives even without any role rows).
    REQUIRE((*info)["roles"].isArray() == true);
    CHECK((*info)["roles"].size() == 0u);

    cleanupUser(db, user.id);
}

DROGON_TEST(Integration_P0_Contract_Functional_UserInfoRepository_Postgres_UnknownUser_ReturnsNullopt)
{
    auto db = getPostgresClientOrNull();
    if (!db)
        return;

    auto repo = std::make_shared<PostgresIdentityRepository>(db);
    // SERIAL ids are positive; -1 can never exist.
    auto info = fetchUserInfo(repo, -1);
    CHECK(info.has_value() == false);
}

// ===========================================================================
// Memory
// ===========================================================================

DROGON_TEST(Integration_P0_Contract_Functional_UserInfoRepository_Memory_MappedUser_SyntheticClaimsWithConfiguredRoles)
{
    MemoryIdentityRepository repo;
    Json::Value adminCfg;
    Json::Value roles(Json::arrayValue);
    roles.append("admin");
    roles.append("user");
    adminCfg["42"] = roles;
    repo.initAdminRoles(adminCfg);

    auto info = waitForValue<std::optional<Json::Value>>([&](auto cb) {
        repo.getUserInfoWithRoles(42, std::move(cb));
    });

    REQUIRE(info.has_value() == true);
    // Memory backend synthesizes claims from the numeric id (no user table).
    CHECK((*info)["sub"].asString() == "42");
    CHECK((*info)["name"].asString() == "user_42");
    CHECK((*info)["email"].asString() == "user_42@example.com");
    REQUIRE((*info)["roles"].isArray() == true);
    REQUIRE((*info)["roles"].size() == 2u);
    CHECK((*info)["roles"][0].asString() == "admin");
    CHECK((*info)["roles"][1].asString() == "user");
}

DROGON_TEST(Integration_P0_Contract_Functional_UserInfoRepository_Memory_UnmappedUser_DefaultsToUserRole_NeverNullopt)
{
    // No initAdminRoles call at all: even a completely unknown id gets
    // synthetic claims + the {"user"} default role (documented divergence
    // from Postgres, which returns nullopt for unknown users).
    MemoryIdentityRepository repo;

    auto info = waitForValue<std::optional<Json::Value>>([&](auto cb) {
        repo.getUserInfoWithRoles(7, std::move(cb));
    });

    REQUIRE(info.has_value() == true);
    CHECK((*info)["sub"].asString() == "7");
    REQUIRE((*info)["roles"].isArray() == true);
    REQUIRE((*info)["roles"].size() == 1u);
    CHECK((*info)["roles"][0].asString() == "user");
}
