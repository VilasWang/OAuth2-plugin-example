#pragma once

// Task 19 (authforge-sdk-refactor, design.md §5.1/§5.4/§6): Adapter-layer
// Postgres implementation of the three identity-owned repository
// interfaces (IUserRepository / IRoleRepository / ISubjectMappingRepository,
// libs/identity). Lives in libs/storage-postgres (not libs/identity)
// because it depends on drogon::orm model types (design.md §4.1 rule 1:
// Domain forbids drogon::orm) -- same placement rationale as every other
// PostgresXxxRepository in this codebase.
//
// This is a NEW class, not a relocation of the existing
// oauth2::PostgresUserRepository/PostgresRoleRepository/
// PostgresSubjectMappingRepository (OAuth2Plugin/{include,src}/oauth2/
// storage/): those remain in place and continue to back
// authforge::identity::IdentityService (still the production path for the
// controllers this migration slice deliberately leaves untouched --
// MFA/WebAuthn/Social/Session). This class is the identity SDK's OWN
// backing store, used only by the new authforge::identity::AuthService /
// RoleProvider / SubjectResolver / UserInfoProvider wiring. The query
// shapes are equivalent (same tables/columns) but the two implementations
// are intentionally decoupled -- unifying them is a larger follow-up
// slice (would require migrating IdentityService's callers too), not
// this one.

#include <authforge/identity/IUserRepository.h>
#include <authforge/identity/IRoleRepository.h>
#include <authforge/identity/ISubjectMappingRepository.h>

#include <drogon/orm/DbClient.h>

#include <memory>

namespace authforge::storage::postgres
{

/**
 * @brief Postgres-backed implementation of identity's user, role, and
 * subject-mapping repository interfaces.
 *
 * Single class implementing all three (rather than three separate
 * classes) because they share one DB client and the underlying queries
 * are small -- no per-aggregate splitting benefit here, unlike the
 * larger oauth2 storage split (Task 9), which exists to break up 1743
 * lines of interleaved logic. This class starts small; if it grows,
 * split it then.
 */
class PostgresIdentityRepository : public authforge::identity::IUserRepository,
                                   public authforge::identity::IRoleRepository,
                                   public authforge::identity::ISubjectMappingRepository,
                                   public std::enable_shared_from_this<PostgresIdentityRepository>
{
  public:
    // M3 Task 20 pitfall (see PROGRESS.md's "authforge::drogon::* 命名空间
    // 裸写" note): globally qualified (::drogon::orm::DbClientPtr) so this
    // header compiles correctly in any translation unit that also
    // includes an authforge::drogon::* header (e.g. bootstrap/
    // IdentityAssembly.cc, which includes
    // authforge/drogon/controllers/SessionController.h in the same TU) --
    // an unqualified `drogon::` here would resolve to the sibling
    // authforge::drogon namespace instead of the global ::drogon one.
    explicit PostgresIdentityRepository(::drogon::orm::DbClientPtr dbClient)
        : dbClient_(std::move(dbClient))
    {
    }

    // --- IUserRepository ---
    void findByEmail(
      const std::string &email,
      std::function<void(std::optional<authforge::identity::UserData>)> &&callback
    ) override;

    void findByUsername(
      const std::string &username,
      std::function<void(std::optional<authforge::identity::UserData>)> &&callback
    ) override;

    void findById(
      int32_t userId,
      std::function<void(std::optional<authforge::identity::UserData>)> &&callback
    ) override;

    void findByPublicSub(
      const std::string &publicSub,
      std::function<void(std::optional<authforge::identity::UserData>)> &&callback
    ) override;

    void create(
      const authforge::identity::UserData &userData,
      std::function<void(std::optional<int32_t>, std::string errorCode)> &&callback
    ) override;

    void updatePasswordHash(
      int32_t userId,
      const std::string &newHash,
      std::function<void(bool)> &&callback
    ) override;

    void resetFailedLogins(int32_t userId, std::function<void(bool)> &&callback) override;

    void incrementFailedLogins(int32_t userId, std::function<void(bool)> &&callback) override;

    void getUserInfoWithRoles(
      int32_t userId,
      std::function<void(std::optional<Json::Value>)> &&callback
    ) override;

    // --- IRoleRepository ---
    void getRoles(
      int32_t internalUserId,
      std::function<void(std::vector<std::string>)> &&cb
    ) override;

    void getRoles(
      const std::string &subject,
      std::function<void(std::vector<std::string>)> &&cb
    ) override;

    // --- ISubjectMappingRepository ---
    void getInternalUserId(
      const std::string &subject,
      const std::string &provider,
      std::function<void(std::optional<int32_t>)> &&cb
    ) override;

    void createSubjectMapping(
      const std::string &subject,
      int32_t internalUserId,
      const std::string &provider,
      std::function<void(bool)> &&cb
    ) override;

    void createUserForExternalLogin(
      const std::string &externalId,
      const std::string &provider,
      std::function<void(std::optional<int32_t>)> &&cb
    ) override;

  private:
    ::drogon::orm::DbClientPtr dbClient_;
};

}  // namespace authforge::storage::postgres
