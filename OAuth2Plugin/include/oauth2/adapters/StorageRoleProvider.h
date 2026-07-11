#pragma once

// M2b Task 17 slice 12 (authforge-sdk-refactor): Adapter-side default
// implementation of authforge::common::ports::IRoleProvider, backed by
// the existing IOAuth2Storage::getUserRoles(int32_t, ...) overload. This
// is the first production wiring of IRoleProvider (declared in Task 13,
// libs/common) -- until now nothing in production implemented it.
//
// Thin forwarding adapter: takes a shared IOAuth2Storage (same pattern as
// every other *Service class in this codebase) and forwards
// getRoles(internalUserId, cb) to storage->getUserRoles(internalUserId,
// cb) verbatim. Placed under OAuth2Plugin/include/oauth2/adapters/
// alongside the other Adapter-layer port implementations
// (OpenSslCryptoProvider, DrogonLogger, DrogonAuditSink) for the same
// reason: libs/drogon does not exist yet (M3, Task 20).

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
