#include <authforge/identity/ISubjectResolver.h>

namespace authforge::identity
{

using authforge::common::model::Subject;

// Placeholder implementation - will be implemented in follow-up tasks
class SubjectResolver : public ISubjectResolver
{
public:
    void resolveSubject(
      const Subject &subject,
      std::function<void(std::optional<int64_t>)> &&callback
    ) override
    {
        // TODO: Implement - query oauth2_subject_mappings table
        callback(std::nullopt);
    }

    void createSubjectMapping(
      const Subject &subject,
      int64_t internalUserId,
      std::function<void(bool)> &&callback
    ) override
    {
        // TODO: Implement - insert into oauth2_subject_mappings
        callback(false);
    }

    void getSubjectByUserId(
      int64_t internalUserId,
      const std::optional<std::string> &provider,
      std::function<void(std::optional<Subject>)> &&callback
    ) override
    {
        // TODO: Implement - query oauth2_subject_mappings by user_id
        callback(std::nullopt);
    }
};

}  // namespace authforge::identity
