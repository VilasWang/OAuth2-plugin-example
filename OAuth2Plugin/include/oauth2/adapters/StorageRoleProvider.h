#pragma once

// M2b Task 17 slice 12 (authforge-sdk-refactor): Adapter-side default
// implementation of authforge::common::ports::IRoleProvider. Originally backed
// only by IOAuth2Storage::getUserRoles(int32_t, ...).
//
// Phase 4.5: now ALSO overrides the subject-string overload
// (supportsSubjectLookup() = true), backed by
// IOAuth2Storage::getUserRoles(const std::string&, ...). This preserves the
// legacy "roles keyed by subject string" semantics (MemoryOAuth2Storage /
// MemoryRoleRepository userRoles_ map, populated from admin_users config)
// through the new IRoleProvider port, letting oauth2::protocol::TokenService
// resolve config-only subjects like "admin" without a subject_mapping row --
// which retires LegacyRoleResolutionBridge's synthetic-id/pending-roles shim.
//
// Thin forwarding adapter: takes a shared IOAuth2Storage (same pattern as
// every other *Service class in this codebase) and forwards getRoles(...)
// to storage->getUserRoles(...). Placed under OAuth2Plugin/include/oauth2/
// adapters/ alongside the other Adapter-layer port implementations
// (OpenSslCryptoProvider, DrogonLogger, DrogonAuditSink).

#include <authforge/common/ports/IRoleProvider.h>
#include <oauth2/storage/IOAuth2Storage.h>

#include <memory>

namespace oauth2::adapters
{

class StorageRoleProvider : public authforge::common::ports::IRoleProvider
{
  public:
    explicit StorageRoleProvider(std::shared_ptr<oauth2::IOAuth2Storage> storage)
        : storage_(std::move(storage))
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
        if (!storage_)
        {
            cb({});
            return;
        }
        storage_->getUserRoles(subject, std::move(cb));
    }

    void getRoles(int32_t internalUserId, RolesCallback &&cb) override
    {
        if (!storage_)
        {
            cb({});
            return;
        }
        storage_->getUserRoles(internalUserId, std::move(cb));
    }

  private:
    std::shared_ptr<oauth2::IOAuth2Storage> storage_;
};

}  // namespace oauth2::adapters
