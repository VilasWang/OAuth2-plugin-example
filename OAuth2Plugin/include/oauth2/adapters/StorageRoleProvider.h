#pragma once

// M2b Task 17 slice 12 (authforge-sdk-refactor): Adapter-side default
// implementation of authforge::common::ports::IRoleProvider.
//
// Phase 4.5: overrides the subject-string overload (supportsSubjectLookup()
// = true), preserving the legacy "roles keyed by subject string" semantics
// (MemoryRoleRepository userRoles_ populated from admin_users config) through
// the new IRoleProvider port.
//
// Phase 4.6a: now backed by oauth2::IRoleRepository (the identity split-repo)
// instead of the god IOAuth2Storage facade. Forwards getRoles(...) to
// roleRepo->getUserRoles(...). Placed under OAuth2Plugin/include/oauth2/
// adapters/ alongside the other Adapter-layer port implementations.

#include <authforge/common/ports/IRoleProvider.h>
#include <oauth2/storage/IRoleRepository.h>

#include <memory>

namespace oauth2::adapters
{

class StorageRoleProvider : public authforge::common::ports::IRoleProvider
{
  public:
    explicit StorageRoleProvider(std::shared_ptr<oauth2::IRoleRepository> roleRepo)
        : roleRepo_(std::move(roleRepo))
    {
    }

    // Phase 4.5: subject-string path -- preferred by
    // oauth2::protocol::TokenService. Byte-equivalent to the legacy
    // storage_->getUserRoles(subjectString).
    bool supportsSubjectLookup() const noexcept override
    {
        return true;
    }

    void getRoles(const std::string &subject, RolesCallback &&cb) override
    {
        if (!roleRepo_)
        {
            cb({});
            return;
        }
        roleRepo_->getUserRoles(subject, std::move(cb));
    }

    void getRoles(int32_t internalUserId, RolesCallback &&cb) override
    {
        if (!roleRepo_)
        {
            cb({});
            return;
        }
        roleRepo_->getUserRoles(internalUserId, std::move(cb));
    }

  private:
    std::shared_ptr<oauth2::IRoleRepository> roleRepo_;
};

}  // namespace oauth2::adapters
