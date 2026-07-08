#include <oauth2/storage/MemorySubjectMappingRepository.h>
#include <drogon/drogon.h>

namespace oauth2
{

void MemorySubjectMappingRepository::getInternalUserId(
  const std::string &subject,
  const std::string &provider,
  OptionalIntCallback &&cb
)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string key = provider + ":" + subject;
    auto it = subjectMappings_.find(key);
    if (it != subjectMappings_.end())
    {
        cb(it->second);
    }
    else
    {
        cb(std::nullopt);
    }
}

void MemorySubjectMappingRepository::createSubjectMapping(
  const std::string &subject,
  int32_t internalUserId,
  const std::string &provider,
  BoolCallback &&cb
)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string key = provider + ":" + subject;
    subjectMappings_[key] = internalUserId;
    LOG_DEBUG << "Created subject mapping: " << key << " -> " << internalUserId;
    cb(true);
}

}  // namespace oauth2
