#include <fulla/drogon/adapters/StorageSubjectResolver.h>
#include <fulla/drogon/utils/SubjectGenerator.h>

namespace fulla::drogon::adapters
{

void StorageSubjectResolver::resolve(
  const fulla::common::model::Subject &subject,
  ResolveCallback &&cb
)
{
    if (!subjectMappingRepo_)
    {
        cb(std::nullopt);
        return;
    }

    // Subject format is "provider:localId" (e.g. "local:alice",
    // "google:google_sub") -- mirror IdentityService::getInternalUserId's parse
    // (the production path this Adapter replaces).
    auto [provider, sub] = ::fulla::common::utils::SubjectGenerator::parse(subject.value());
    subjectMappingRepo_->getInternalUserId(sub, provider, std::move(cb));
}

}  // namespace fulla::drogon::adapters
