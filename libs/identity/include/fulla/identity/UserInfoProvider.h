#pragma once

// Task 19 (fulla-sdk-refactor, design.md §5.1/§5.2/§6): identity's
// production implementation of fulla::common::ports::IUserInfoProvider,
// backed by IUserRepository::getUserInfoWithRoles (already returns the
// sub/name/email/roles claims bag AuthService::getUserInfo delegates to).
// See RoleProvider.h's header comment for the general 方案 A rationale.

#include <fulla/common/ports/IUserInfoProvider.h>
#include <fulla/identity/IUserRepository.h>

#include <memory>

namespace fulla::identity
{

class UserInfoProvider : public fulla::common::ports::IUserInfoProvider
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

}  // namespace fulla::identity
