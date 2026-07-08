#pragma once

// Task 13 (authforge-sdk-refactor, design.md §5.2/§5.3/§6): port interfaces
// for the shared Domain kernel.
//
// IRoleProvider is the port oauth2's scope-tiering policy (design.md §5.3:
// "scope 分层策略（'scope X 需 admin'）") uses to ask "what roles does this
// user have" without oauth2 compiling a dependency on libs/identity. See
// ISubjectResolver.h for the full 方案 A rationale (端口下沉到 common) and
// the async-callback design-consistency note -- both apply identically
// here (identity's future implementation is expected to be backed by the
// existing IRoleRepository::getUserRoles, which is async).
//
// Deliberately keyed by internal user id (int32_t), matching
// ISubjectResolver's resolve() output type and the existing
// IRoleRepository::getUserRoles(int32_t internalUserId, ...) overload --
// so the oauth2-side call sequence is
// "ISubjectResolver::resolve(subject) -> IRoleProvider::getRoles(userId)"
// without an intermediate re-wrapping step.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace authforge::common::ports
{

/**
 * @brief Provides the role list assigned to an internal user id.
 */
class IRoleProvider
{
  public:
    using RolesCallback = std::function<void(std::vector<std::string>)>;

    virtual ~IRoleProvider() = default;

    /**
     * @brief Get the roles assigned to a user by internal user id.
     * Invokes `cb` with the role-name list (empty if the user has no
     * roles or does not exist).
     */
    virtual void getRoles(int32_t internalUserId, RolesCallback &&cb) = 0;
};

}  // namespace authforge::common::ports
