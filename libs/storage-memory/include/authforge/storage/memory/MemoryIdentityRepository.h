#pragma once

// Phase 1.5c (Task 39, authforge-sdk-refactor, design.md §5.4/§6): the
// in-memory implementation of the three identity-owned repository interfaces
// (IUserRepository / IRoleRepository / ISubjectMappingRepository, libs/identity),
// mirroring the legacy oauth2::Memory{User,Role,SubjectMapping}Repository
// (OAuth2Plugin/storage/) that back the config.json "memory" storage_type
// (the dev-server default).
//
// Mirrors the legacy behavior:
//  - getRoles: config-driven admin_users role map (the real working logic of
//    the legacy MemoryRoleRepository), defaulting to {"user"}.
//  - getInternalUserId / createSubjectMapping: in-memory provider:subject ->
//    internal-id map (the real working logic of the legacy
//    MemorySubjectMappingRepository).
//  - findById / findByPublicSub / findByEmail / findByUsername: synthesizes a
//    placeholder UserData (the legacy MemoryUserRepository was also a stateless
//    placeholder). create / updatePasswordHash / reset* / increment* are
//    best-effort no-ops (memory backend has no real user store) -- consistent
//    with the legacy memory backend, which never persisted users either.
//
// createUserForExternalLogin inherits the ISubjectMappingRepository default
// (returns nullopt) -- the memory backend does not auto-create users.
//
// Like the sibling Memory{Client,Grant,Token,Consent}Repository in this
// package, this class does NOT inherit enable_shared_from_this: its callbacks
// are synchronous/inline (no async continuation crosses an event-loop
// boundary).

#include <authforge/identity/IUserRepository.h>
#include <authforge/identity/IRoleRepository.h>
#include <authforge/identity/ISubjectMappingRepository.h>

#include <json/json.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace authforge::storage::memory
{

class MemoryIdentityRepository : public authforge::identity::IUserRepository,
                                 public authforge::identity::IRoleRepository,
                                 public authforge::identity::ISubjectMappingRepository
{
  public:
    /// Populate the admin role map from config.json's "admin_users" block
    /// (mirrors the legacy MemoryRoleRepository::initFromConfig). Shape:
    /// { "admin": ["admin","user"], ... }. When null/empty, defaults the
    /// "admin" user to {"admin","user"} (legacy backward-compat).
    void initAdminRoles(const Json::Value &adminConfig);

    // --- IRoleRepository ---
    void getRoles(
      int32_t internalUserId,
      authforge::identity::IRoleRepository::RolesCallback &&cb
    ) override;
    void getRoles(
      const std::string &subject,
      authforge::identity::IRoleRepository::RolesCallback &&cb
    ) override;

    // --- ISubjectMappingRepository ---
    void getInternalUserId(
      const std::string &subject,
      const std::string &provider,
      authforge::identity::ISubjectMappingRepository::OptionalIntCallback &&cb
    ) override;
    void createSubjectMapping(
      const std::string &subject,
      int32_t internalUserId,
      const std::string &provider,
      authforge::identity::ISubjectMappingRepository::BoolCallback &&cb
    ) override;

    // --- IUserRepository (placeholder/synthetic, like the legacy memory backend) ---
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

  private:
    // Build a synthetic placeholder UserData for a numeric id (mirrors the
    // legacy MemoryUserRepository synthesis).
    authforge::identity::UserData syntheticUser(int32_t id) const;

    std::map<std::string, std::vector<std::string>>
      userRoles_;  // keyed by subject string (legacy quirk: int ids converted to string)
    std::map<std::string, int32_t> subjectMappings_;  // key "provider:subject"
    mutable std::recursive_mutex mutex_;
};

}  // namespace authforge::storage::memory
