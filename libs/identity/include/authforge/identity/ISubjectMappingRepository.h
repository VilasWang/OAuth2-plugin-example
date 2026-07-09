#pragma once

// Task 19 (authforge-sdk-refactor, design.md §5.1/§6): identity-owned
// repository interface backing SubjectResolver's implementation of
// authforge::common::ports::ISubjectResolver. Mirrors the read-side shape
// of the existing oauth2::ISubjectMappingRepository
// (OAuth2Plugin/include/oauth2/storage/ISubjectMappingRepository.h), which
// per that header's own comment is "conceptually part of the identity
// domain" but was left under OAuth2Plugin/oauth2:: pending this exact
// migration (M2.5, Task 19).
//
// Scope note: only the READ path (getInternalUserId) is declared here --
// resolve() is the only method authforge::common::ports::ISubjectResolver
// requires, and the write path (createSubjectMapping /
// createUserForExternalLogin, used by social-login registration flows) is
// out of scope for this migration slice, which is deliberately bounded to
// AuthService + RBAC/subject-resolution binding (the social/WebAuthn/MFA
// controllers that would drive the write path stay on the existing
// OAuth2Plugin implementation for now, per this slice's scope).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace authforge::identity
{

/**
 * @brief Read-side repository for external-subject -> internal-user-id
 * mapping.
 */
class ISubjectMappingRepository
{
  public:
    virtual ~ISubjectMappingRepository() = default;

    using OptionalIntCallback = std::function<void(std::optional<int64_t>)>;

    /**
     * @brief Get the internal user id for an OAuth2/OIDC subject +
     * provider pair.
     * @param subject Subject identifier (within provider scope).
     * @param provider Provider name ("local", "google", "wechat", etc.).
     * @param cb Callback with the internal user id, or nullopt if no
     * mapping exists.
     */
    virtual void getInternalUserId(
      const std::string &subject,
      const std::string &provider,
      OptionalIntCallback &&cb
    ) = 0;
};

}  // namespace authforge::identity
