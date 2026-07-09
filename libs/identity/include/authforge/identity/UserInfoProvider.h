#pragma once

// Task 19 (authforge-sdk-refactor, design.md §5.1/§5.2/§6): identity's
// production implementation of authforge::common::ports::IUserInfoProvider,
// backed by IUserRepository::getUserInfoWithRoles (already returns the
// sub/name/email/roles claims bag AuthService::getUserInfo delegates to).
// See RoleProvider.h's header comment for the general 方案 A rationale.

#include <authforge/common/ports/IUserInfoProvider.h>
#include <authforge/identity/IUserRepository.h>

#include <memory>

namespace authforge::identity
{

class UserInfoProvider : public authforge::common::ports::IUserInfoProvider
{
  public:
    explicit UserInfoProvider(std::shared_ptr<IUserRepository> repo) : repo_(std::move(repo))
    {
    }

    void getUserInfo(int32_t internalUserId, UserInfoCallback &&cb) override
    {
        if (!repo_)
        {
            cb(std::nullopt);
            return;
        }
        repo_->getUserInfoWithRoles(internalUserId, std::move(cb));
    }

  private:
    std::shared_ptr<IUserRepository> repo_;
};

}  // namespace authforge::identity
