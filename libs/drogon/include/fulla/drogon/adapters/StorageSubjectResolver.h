#pragma once

// B10 / Task 45 (fulla-sdk-refactor, design.md §5.2/§5.3): Adapter-side
// default implementation of fulla::common::ports::ISubjectResolver, backed
// by fulla::identity::ISubjectMappingRepository (Phase 1.5d: was the
// legacy ::oauth2::ISubjectMappingRepository). Mirrors StorageRoleProvider's
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

#include <fulla/common/model/Subject.h>
#include <fulla/common/ports/ISubjectResolver.h>
#include <fulla/identity/ISubjectMappingRepository.h>

#include <memory>

namespace fulla::drogon::adapters
{

class StorageSubjectResolver : public fulla::common::ports::ISubjectResolver
{
  public:
    explicit StorageSubjectResolver(
      std::shared_ptr<fulla::identity::ISubjectMappingRepository> subjectMappingRepo
    )
        : subjectMappingRepo_(std::move(subjectMappingRepo))
    {
    }

    void resolve(const fulla::common::model::Subject &subject, ResolveCallback &&cb) override;

  private:
    std::shared_ptr<fulla::identity::ISubjectMappingRepository> subjectMappingRepo_;
};

}  // namespace fulla::drogon::adapters
