#pragma once

// Task 19 (authforge-sdk-refactor, design.md §5.1/§5.3/§6): identity-owned
// repository interface backing RoleProvider's implementation of
// authforge::common::ports::IRoleProvider (design.md §5.3: "RBAC 数据
// （roles/permissions/user-role）→ identity（实现
// common::ports::IRoleProvider）").
//
// Phase 1.5b (Task 39): added the subject-string `getRoles` overload so this
// interface becomes a superset of the legacy oauth2::IRoleRepository (which
// StorageRoleProvider's subject-string path depends on). The string is
// resolved to the internal id inside the implementation (numeric -> int32
// directly; otherwise treated as users.public_sub).

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
    virtual void getRoles(int32_t internalUserId, RolesCallback &&cb) = 0;

    /**
     * @brief Get the role names assigned to a user by subject string.
     *
     * `subject` is either a numeric internal id (resolved directly) or the
     * users.public_sub UUID (resolved to the internal id first, then queried).
     * This overload backs StorageRoleProvider's subject-string path, which
     * oauth2::protocol::TokenService prefers over the internal-id path.
     */
    virtual void getRoles(const std::string &subject, RolesCallback &&cb) = 0;
};

}  // namespace authforge::identity
