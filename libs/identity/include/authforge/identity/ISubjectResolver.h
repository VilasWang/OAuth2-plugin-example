#pragma once

#include <authforge/common/model/Subject.h>
#include <functional>
#include <optional>
#include <string>

namespace authforge::identity
{

// Import Subject into this namespace for convenience
using authforge::common::model::Subject;

/**
 * @brief Interface for resolving external subjects to internal user IDs
 * 
 * Maps external subject identifiers (from OAuth2, SAML, etc.) to internal user IDs.
 * This abstraction allows OAuth2 SDK to work with Subject without exposing internal DB IDs.
 */
class ISubjectResolver
{
public:
    virtual ~ISubjectResolver() = default;

    /**
     * @brief Resolve an external subject to internal user ID
     * @param subject The external subject (provider + external ID)
     * @param callback Async callback with optional internal user ID
     */
    virtual void resolveSubject(
      const Subject &subject,
      std::function<void(std::optional<int64_t>)> &&callback
    ) = 0;

    /**
     * @brief Create a new subject mapping
     * @param subject The external subject to map
     * @param internalUserId The internal user ID to map to
     * @param callback Async callback indicating success
     */
    virtual void createSubjectMapping(
      const Subject &subject,
      int64_t internalUserId,
      std::function<void(bool)> &&callback
    ) = 0;

    /**
     * @brief Get external subject from internal user ID
     * @param internalUserId The internal user ID
     * @param provider The OAuth provider (optional filter)
     * @param callback Async callback with optional Subject
     */
    virtual void getSubjectByUserId(
      int64_t internalUserId,
      const std::optional<std::string> &provider,
      std::function<void(std::optional<Subject>)> &&callback
    ) = 0;
};

}  // namespace authforge::identity
