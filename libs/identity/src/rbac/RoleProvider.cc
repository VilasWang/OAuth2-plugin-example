#include <authforge/identity/IRoleProvider.h>

namespace authforge::identity
{

using authforge::common::model::Subject;

// Placeholder implementation - will be implemented in follow-up tasks
class RoleProvider : public IRoleProvider
{
public:
    void getUserRoles(
      const Subject &subject,
      std::function<void(std::vector<std::string>)> &&callback
    ) override
    {
        // TODO: Implement
        callback({});
    }

    void hasRole(
      const Subject &subject,
      const std::string &roleName,
      std::function<void(bool)> &&callback
    ) override
    {
        // TODO: Implement
        callback(false);
    }

    void getUserPermissions(
      const Subject &subject,
      std::function<void(std::vector<std::string>)> &&callback
    ) override
    {
        // TODO: Implement
        callback({});
    }
};

}  // namespace authforge::identity
