#pragma once

// Task 15 (authforge-sdk-refactor, design.md §6/§8): fake implementations
// of common::ports interfaces. See FakeClock.h for the placement
// rationale.
//
// FakeRoleProvider: an in-memory IRoleProvider for oauth2 Domain tests
// (M2b+) exercising scope-tiering policy against a controllable role set,
// without a real identity implementation.

#include <authforge/common/ports/IRoleProvider.h>

#include <unordered_map>

namespace authforge::common::testing
{

class FakeRoleProvider : public authforge::common::ports::IRoleProvider
{
  public:
    void getRoles(int32_t internalUserId, RolesCallback &&cb) override
    {
        auto it = roles_.find(internalUserId);
        if (it == roles_.end())
        {
            cb({});
            return;
        }
        cb(it->second);
    }

    /// Set the role list returned for `internalUserId`.
    void setRoles(int32_t internalUserId, std::vector<std::string> roles)
    {
        roles_[internalUserId] = std::move(roles);
    }

    /// Remove all registered role assignments.
    void clear()
    {
        roles_.clear();
    }

  private:
    std::unordered_map<int32_t, std::vector<std::string>> roles_;
};

}  // namespace authforge::common::testing
