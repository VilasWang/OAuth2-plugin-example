#pragma once

// M1 storage interface split (design.md §7). See IClientRepository.h header
// comment for the general rationale (additive, non-migrating, see
// REPOSITORY_MAPPING.md for the full mapping).
//
// IMPORTANT -- physical location vs. domain ownership (Task 8 scope note):
// this interface is conceptually part of the *identity* domain (design.md
// §7.1: subject↔internal-user mapping → `ISubjectMappingRepository`,
// grouped under identity), but Task 8 does NOT move it into libs/identity
// or the authforge::identity namespace. That directory and namespace
// migration is M2.5 / Task 19. For now this header stays physically under
// OAuth2Plugin/include/oauth2/storage/ and in namespace `oauth2`, exactly
// like the M1 oauth2 repositories from Task 7 and IUserRepository.h /
// IRoleRepository.h from this task, purely to carve the method group out
// of IOAuth2Storage without a big-bang move. Do not treat this file's
// current location/namespace as "identity has already been extracted" --
// it has not; that is future work.
//
// Forward-looking note: once libs/identity exists (Task 19), an
// implementation of this repository is expected to sit behind (and feed)
// a future common::ports::ISubjectResolver port (design.md §5.2:
// "subject（`local:alice`）→ 内部 userId"). This header does NOT declare or
// depend on ISubjectResolver today -- that port lives in a package that
// does not exist yet, and creating a real dependency on it here is
// explicitly out of scope for Task 8. This comment exists purely to
// document the intended future relationship for whoever does the Task 19
// migration.
//
// IOAuth2Storage.h is reused here for the OptionalIntCallback/BoolCallback
// aliases (both are declared as public nested `using`s inside
// IOAuth2Storage, so they are reachable via the qualified names
// `IOAuth2Storage::OptionalIntCallback` / `IOAuth2Storage::BoolCallback`
// from outside the class -- verified by mirroring the same pattern
// IGrantRepository.h uses for `IOAuth2Storage::AuthorizationTransaction`
// and IUserRepository.h uses for `IOAuth2Storage::OptionalJsonCallback`).
// This avoids defining second, competing `std::function<...>` aliases with
// the same shapes.
#include <oauth2/storage/IOAuth2Storage.h>

#include <cstdint>
#include <string>

namespace oauth2
{

/**
 * @brief Repository for external-subject ↔ internal-user-id mapping -- the
 * "subject mapping" aggregate (how an OAuth2/OIDC subject, e.g.
 * "google:12345", resolves to (and is created against) an internal user
 * record).
 *
 * Carves out the getInternalUserId() / createSubjectMapping() /
 * createUserForExternalLogin() method group from the former god interface
 * IOAuth2Storage. See REPOSITORY_MAPPING.md for the full mapping
 * (entries #19-21).
 *
 * Domain ownership (design.md §7.1): this belongs to the identity SDK
 * package once libs/identity exists (M2.5, Task 19); see the file header
 * comment above for why it is not physically there yet.
 *
 * SDK consumers MAY implement this interface (design.md §7.1: identity
 * repositories are "可选（不集成身份则不实现）") -- a deployment that does
 * not integrate external/federated login can skip it.
 */
class ISubjectMappingRepository
{
  public:
    virtual ~ISubjectMappingRepository() = default;

    using OptionalIntCallback = IOAuth2Storage::OptionalIntCallback;
    using BoolCallback = IOAuth2Storage::BoolCallback;

    /**
     * @brief Get internal user ID by OAuth2/OpenID Connect subject and
     * provider.
     * Original: IOAuth2Storage::getInternalUserId
     *
     * @param subject OAuth2/OpenID Connect subject (within provider scope)
     * @param provider Provider name ('local', 'google', 'wechat', etc.)
     * @param cb Callback with internal user ID or std::nullopt if not found
     */
    virtual void getInternalUserId(
      const std::string &subject,
      const std::string &provider,
      OptionalIntCallback &&cb
    ) = 0;

    /**
     * @brief Create a new subject mapping.
     * Original: IOAuth2Storage::createSubjectMapping
     *
     * @param subject OAuth2/OpenID Connect subject
     * @param internalUserId Internal user ID from users table
     * @param provider Provider name
     * @param cb Callback invoked with true on success, false on failure
     */
    virtual void createSubjectMapping(
      const std::string &subject,
      int32_t internalUserId,
      const std::string &provider,
      BoolCallback &&cb
    ) = 0;

    /**
     * @brief Create a user record for external (third-party) login.
     * Original: IOAuth2Storage::createUserForExternalLogin
     *
     * NOTE (preserved default, not a new Task 8 decision): this keeps the
     * exact default implementation from IOAuth2Storage --
     * `cb(std::nullopt);` -- meaning "not supported" for backends that
     * cannot mint a new user row on first external login (Memory/Redis).
     * This is a carry-over of the original god-interface's default
     * behaviour, not something newly designed for this interface; a
     * backend that DOES support it (e.g. Postgres) is expected to override
     * it, exactly as PostgresOAuth2Storage overrides
     * IOAuth2Storage::createUserForExternalLogin today.
     *
     * @param externalId External user identifier (e.g., Google sub)
     * @param provider Provider name (google, wechat, etc.)
     * @param cb Callback with new internal user ID, or nullopt on failure
     */
    virtual void createUserForExternalLogin(
      const std::string &externalId,
      const std::string &provider,
      OptionalIntCallback &&cb
    )
    {
        // Default implementation: not supported (for Memory/Redis backends).
        // Preserved verbatim from IOAuth2Storage::createUserForExternalLogin.
        cb(std::nullopt);
    }

    // ---------------------------------------------------------------------
    // Decision (see REPOSITORY_MAPPING.md): NO purgeExpired() here.
    //
    // Verified against all three existing IOAuth2Storage::deleteExpiredData()
    // implementations (PostgresOAuth2Storage.cc, RedisOAuth2Storage.cc,
    // MemoryOAuth2Storage.cc): none of them touch subject-mapping data.
    // Postgres only sweeps oauth2_codes / oauth2_access_tokens /
    // oauth2_refresh_tokens (+ archives old tokens); Redis is a documented
    // no-op relying on key TTL; Memory only sweeps its authCodes_ /
    // accessTokens_ / refreshTokens_ maps. Subject mappings have no
    // expiresAt/TTL semantic in the current model -- once created, a
    // mapping is permanent until the underlying user is deleted (not a
    // concern this interface currently models). Intentionally omitted for
    // the same reason IUserRepository/IRoleRepository omit it: a no-op
    // purgeExpired() would be boilerplate with no corresponding caller
    // need.
    // ---------------------------------------------------------------------
};

}  // namespace oauth2
