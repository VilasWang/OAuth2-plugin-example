#pragma once

// Task 19 (fulla-sdk-refactor, design.md §5.1/§5.3/§6): identity's
// production implementation of fulla::common::ports::IRoleProvider,
// backed by IRoleRepository. This is the "identity implements the
// common-owned port" half of design.md §5.2's 方案 A (端口下沉到 common):
// oauth2's scope-tiering policy depends only on the common::ports
// interface; the product layer (a later M3 task) is the one place that
// constructs THIS class and injects it into oauth2's service
// constructors -- identity itself never depends on oauth2 (arch-guard
// enforced, design.md §5.1's identity acceptance criterion).

#include <fulla/common/ports/IRoleProvider.h>
#include <fulla/identity/IRoleRepository.h>

#include <memory>

namespace fulla::identity
{

class RoleProvider : public fulla::common::ports::IRoleProvider
{
  public:
    explicit RoleProvider(std::shared_ptr<IRoleRepository> repo) : repo_(std::move(repo))
    {
    }

    void getRoles(int32_t internalUserId, RolesCallback &&cb) override
    {
        if (!repo_)
        {
            cb({});
            return;
        }
        repo_->getRoles(internalUserId, std::move(cb));
    }

  private:
    std::shared_ptr<IRoleRepository> repo_;
};

}  // namespace fulla::identity
