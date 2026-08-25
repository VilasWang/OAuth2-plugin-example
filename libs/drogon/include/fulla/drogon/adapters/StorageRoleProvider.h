#pragma once

// M2b Task 17 slice 12 (fulla-sdk-refactor): Adapter-side default
// implementation of fulla::common::ports::IRoleProvider.
//
// Phase 4.5: overrides the subject-string overload (supportsSubjectLookup()
// = true), preserving the legacy "roles keyed by subject string" semantics
// (MemoryRoleRepository userRoles_ populated from admin_users config) through
// the new IRoleProvider port.
//
// Phase 1.5d (Task 39): now backed by fulla::identity::IRoleRepository
// (the NEW identity interface; was the legacy ::oauth2::IRoleRepository).
// Forwards getRoles(...) to roleRepo->getRoles(...). Placed under
// OAuth2Plugin/include/oauth2/adapters/ alongside the other Adapter-layer
// port implementations.

#include <fulla/common/ports/IRoleProvider.h>
#include <fulla/identity/IRoleRepository.h>

#include <memory>

namespace fulla::drogon::adapters
{

class StorageRoleProvider : public fulla::common::ports::IRoleProvider
{
  public:
    explicit StorageRoleProvider(std::shared_ptr<fulla::identity::IRoleRepository> roleRepo)
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
        roleRepo_->getRoles(subject, std::move(cb));
    }

    void getRoles(int32_t internalUserId, RolesCallback &&cb) override
    {
        if (!roleRepo_)
        {
            cb({});
            return;
        }
        roleRepo_->getRoles(internalUserId, std::move(cb));
    }

  private:
    std::shared_ptr<fulla::identity::IRoleRepository> roleRepo_;
};

}  // namespace fulla::drogon::adapters
