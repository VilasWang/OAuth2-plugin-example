#pragma once

#include <authforge/common/model/Subject.h>
#include <functional>
#include <string>
#include <vector>

namespace authforge::identity
{

// Import Subject into this namespace for convenience
using authforge::common::model::Subject;

/**
 * @brief Interface for role management and querying
 * 
 * Provides role-based access control (RBAC) capabilities for subjects.
 */
class IRoleProvider
{
public:
    virtual ~IRoleProvider() = default;

    /**
     * @brief Get all roles assigned to a subject
     * @param subject The subject to query roles for
     * @param callback Async callback with list of role names
     */
    virtual void getUserRoles(
      const Subject &subject,
      std::function<void(std::vector<std::string>)> &&callback
    ) = 0;

    /**
     * @brief Check if a subject has a specific role
     * @param subject The subject to check
     * @param roleName The role name to check for
     * @param callback Async callback with boolean result
     */
    virtual void hasRole(
      const Subject &subject,
      const std::string &roleName,
      std::function<void(bool)> &&callback
    ) = 0;

    /**
     * @brief Get all permissions granted to a subject through their roles
     * @param subject The subject to query permissions for
     * @param callback Async callback with list of permission names
     */
    virtual void getUserPermissions(
      const Subject &subject,
      std::function<void(std::vector<std::string>)> &&callback
    ) = 0;
};

}  // namespace authforge::identity
