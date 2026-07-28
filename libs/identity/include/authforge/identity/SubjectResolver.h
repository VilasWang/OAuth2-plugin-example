#pragma once

// Task 19 (authforge-sdk-refactor, design.md §5.1/§5.2/§6): identity's
// production implementation of authforge::common::ports::ISubjectResolver,
// backed by ISubjectMappingRepository. See RoleProvider.h's header comment
// for the general 方案 A rationale -- applies identically here.

#include <authforge/common/ports/ISubjectResolver.h>
#include <authforge/identity/ISubjectMappingRepository.h>

#include <memory>
#include <string>
#include <utility>

namespace authforge::identity
{

/**
 * @brief Splits a "provider:localId" Subject value into (provider,
 * localId). Mirrors authforge::common::utils::SubjectGenerator::parse's fallback
 * behavior: no colon (or empty localId) defaults provider to "local"
 * with the whole value treated as the local id.
 */
inline std::pair<std::string, std::string> splitSubject(const std::string &value)
{
    size_t colonPos = value.find(':');
    if (colonPos == std::string::npos || colonPos == 0 || colonPos == value.length() - 1)
    {
        return {"local", value};
    }
    return {value.substr(0, colonPos), value.substr(colonPos + 1)};
}

class SubjectResolver : public authforge::common::ports::ISubjectResolver
{
  public:
    explicit SubjectResolver(std::shared_ptr<ISubjectMappingRepository> repo)
        : repo_(std::move(repo))
    {
    }

    void resolve(const authforge::common::model::Subject &subject, ResolveCallback &&cb) override
    {
        if (!repo_)
        {
            cb(std::nullopt);
            return;
        }
        auto [provider, localId] = splitSubject(subject.value());
        repo_->getInternalUserId(
          localId, provider, [cb = std::move(cb)](std::optional<int32_t> internalUserId) {
              if (!internalUserId)
              {
                  cb(std::nullopt);
                  return;
              }
              cb(*internalUserId);
          }
        );
    }

  private:
    std::shared_ptr<ISubjectMappingRepository> repo_;
};

}  // namespace authforge::identity
