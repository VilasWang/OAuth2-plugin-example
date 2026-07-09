#pragma once

// Task 19 (authforge-sdk-refactor, design.md §5.1/§5.3/§6): identity-owned
// repository interface backing RoleProvider's implementation of
// authforge::common::ports::IRoleProvider (design.md §5.3: "RBAC 数据
// （roles/permissions/user-role）→ identity（实现
// common::ports::IRoleProvider）"). Deliberately narrow (role-name list by
// internal user id only) -- the existing oauth2-side
// IRoleRepository (OAuth2Plugin/include/oauth2/storage/IRoleRepository.h)
// also has a string-userId overload and permission listing is not yet
// modeled anywhere in the codebase, so this interface covers exactly what
// RoleProvider needs today rather than speculatively widening scope.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace authforge::identity
{

/**
 * @brief Repository for role assignments, keyed by internal user id.
 */
class IRoleRepository
{
  public:
    virtual ~IRoleRepository() = default;

    using RolesCallback = std::function<void(std::vector<std::string>)>;

    /**
     * @brief Get the role names assigned to a user by internal user id.
     * Invokes `cb` with the role-name list (empty if the user has no
     * roles or does not exist).
     */
    virtual void getRoles(int64_t internalUserId, RolesCallback &&cb) = 0;
};

}  // namespace authforge::identity
