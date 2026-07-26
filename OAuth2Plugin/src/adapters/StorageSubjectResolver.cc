#include <oauth2/adapters/StorageSubjectResolver.h>
#include <oauth2/utils/SubjectGenerator.h>

namespace authforge::drogon::adapters
{

void StorageSubjectResolver::resolve(
  const authforge::common::model::Subject &subject,
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
    auto [provider, sub] = ::authforge::common::utils::SubjectGenerator::parse(subject.value());
    subjectMappingRepo_->getInternalUserId(sub, provider, std::move(cb));
}

}  // namespace authforge::drogon::adapters
