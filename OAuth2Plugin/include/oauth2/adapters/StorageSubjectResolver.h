#pragma once

// B10 / Task 45 (authforge-sdk-refactor, design.md §5.2/§5.3): Adapter-side
// default implementation of authforge::common::ports::ISubjectResolver, backed
// by the legacy ::oauth2::ISubjectMappingRepository. Mirrors StorageRoleProvider's
// shape/rationale: the AuthorizationService port (ISubjectResolver) resolves an
// opaque Subject ("provider:localId") into identity's internal user id, which
// the engine needs for the consent tier (IConsentRepository is keyed by
// UserRef{internalUserId}) and the role tier (IRoleProvider.getRoles takes
// internalUserId). Same dependency direction as StorageRoleProvider -- this
// Adapter-layer class implements a common::ports interface against a storage
// repo the product owns.
//
// Why this exists now: AuthorizationService::evaluateScopes requires an
// ISubjectResolver to do its Tier 2 (admin role) + Tier 3 (consent) work.
// OAuth2Plugin previously left ISubjectResolver null (TokenService's role path
// resolved via StorageRoleProvider's subject-string overload instead). Wiring
// the engine into /oauth2/authorize (replacing the controller's inline 3-tier
// chain) needs a real ISubjectResolver here.

#include <authforge/common/model/Subject.h>
#include <authforge/common/ports/ISubjectResolver.h>
#include <oauth2/storage/ISubjectMappingRepository.h>

#include <memory>

namespace authforge::drogon::adapters
{

class StorageSubjectResolver : public authforge::common::ports::ISubjectResolver
{
  public:
    explicit StorageSubjectResolver(
      std::shared_ptr<::oauth2::ISubjectMappingRepository> subjectMappingRepo
    )
        : subjectMappingRepo_(std::move(subjectMappingRepo))
    {
    }

    void resolve(const authforge::common::model::Subject &subject, ResolveCallback &&cb) override;

  private:
    std::shared_ptr<::oauth2::ISubjectMappingRepository> subjectMappingRepo_;
};

}  // namespace authforge::drogon::adapters
