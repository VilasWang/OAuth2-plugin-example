#pragma once

// Task 19 (authforge-sdk-refactor, design.md §5.1/§6): identity-owned
// repository interface backing SubjectResolver's implementation of
// authforge::common::ports::ISubjectResolver. Mirrors the shape of the
// legacy oauth2::ISubjectMappingRepository
// (OAuth2Plugin/include/oauth2/storage/ISubjectMappingRepository.h).
//
// Phase 1.5b (Task 39): added the WRITE path (createSubjectMapping /
// createUserForExternalLogin) so this interface becomes a superset of the
// legacy one and IdentityService::handleFirstTimeLogin / ensureSubjectMapping
// can migrate off the legacy interface. Previously read-only because
// resolve() was the only method the ISubjectResolver port required.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace authforge::identity
{

/**
 * @brief Repository for external-subject -> internal-user-id mapping
 * (read + write).
 */
class ISubjectMappingRepository
{
  public:
    virtual ~ISubjectMappingRepository() = default;

    using OptionalIntCallback = std::function<void(std::optional<int32_t>)>;
    using BoolCallback = std::function<void(bool)>;

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

    /**
     * @brief Record the (subject, provider) -> internalUserId mapping.
     * Idempotent w.r.t. the caller: callers check getInternalUserId first
     * and only call this when no mapping exists yet.
     */
    virtual void createSubjectMapping(
      const std::string &subject,
      int32_t internalUserId,
      const std::string &provider,
      BoolCallback &&cb
    ) = 0;

    /**
     * @brief Create (or reuse) a local user account for an external login
     * that has no existing mapping, returning its internal id.
     *
     * Default implementation returns nullopt (backends that don't support
     * user creation, e.g. Redis). The username is derived from
     * provider + externalId; the account has a placeholder password
     * (external auth, no local password). This is the upsert the social
     * registration flows need before createSubjectMapping.
     */
    virtual void createUserForExternalLogin(
      const std::string & /*externalId*/,
      const std::string & /*provider*/,
      OptionalIntCallback &&cb
    )
    {
        cb(std::nullopt);
    }
};

}  // namespace authforge::identity
