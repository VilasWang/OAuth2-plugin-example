#pragma once

// M1 storage interface split (design.md §7). See IClientRepository.h header
// comment for the general rationale (additive, non-migrating, see
// REPOSITORY_MAPPING.md for the full mapping).
//
// IMPORTANT -- physical location vs. domain ownership (Task 8 scope note):
// this interface is conceptually part of the *identity* domain (design.md
// §5.3: "RBAC 数据（roles/permissions/user-role）→ identity（实现
// common::ports::IRoleProvider）"), but Task 8 does NOT move it into
// libs/identity or the authforge::identity namespace. That directory and
// namespace migration is M2.5 / Task 19. For now this header stays
// physically under OAuth2Plugin/include/oauth2/storage/ and in namespace
// `oauth2`, exactly like the M1 oauth2 repositories from Task 7 and
// IUserRepository.h from this task, purely to carve the method group out
// of IOAuth2Storage without a big-bang move. Do not treat this file's
// current location/namespace as "identity has already been extracted" --
// it has not; that is future work.
//
// Forward-looking note: once libs/identity exists (Task 19), the concrete
// implementation of this repository is expected to sit behind (and feed)
// a future common::ports::IRoleProvider port (design.md §5.2/§5.3:
// "userId → 角色列表（供 scope 分层校验）"). This header does NOT declare or
// depend on IRoleProvider today -- that port lives in a package that does
// not exist yet, and creating a real dependency on it here is explicitly
// out of scope for Task 8. This comment exists purely to document the
// intended future relationship for whoever does the Task 19 migration.
//
// IOAuth2Storage.h is reused here for the StringListCallback alias (it is
// declared as a public nested `using` inside IOAuth2Storage, so it is
// reachable via the qualified name `IOAuth2Storage::StringListCallback`
// from outside the class -- verified by mirroring the same pattern
// IGrantRepository.h uses for `IOAuth2Storage::AuthorizationTransaction`
// and IUserRepository.h uses for `IOAuth2Storage::OptionalJsonCallback`).
// This avoids defining a second, competing `std::function<...>` alias with
// the same shape.
#include <oauth2/storage/IOAuth2Storage.h>

#include <cstdint>
#include <string>

namespace oauth2
{

/**
 * @brief Repository for role assignments -- the "role" aggregate (RBAC data
 * as it currently exists in IOAuth2Storage, i.e. user → role-name lookups).
 *
 * Carves out the getUserRoles() method group from the former god interface
 * IOAuth2Storage. See REPOSITORY_MAPPING.md for the full mapping
 * (entries #15-16).
 *
 * Domain ownership (design.md §5.3): this belongs to the identity SDK
 * package once libs/identity exists (M2.5, Task 19); see the file header
 * comment above for why it is not physically there yet.
 *
 * SDK consumers MAY implement this interface (design.md §7.1: identity
 * repositories are "可选（不集成身份则不实现）") -- a deployment that does
 * not integrate RBAC/roles can skip it.
 */
class IRoleRepository
{
  public:
    virtual ~IRoleRepository() = default;

    using StringListCallback = IOAuth2Storage::StringListCallback;

    /**
     * @brief Get roles assigned to a user by external user ID (string form).
     * Original: IOAuth2Storage::getUserRoles(const std::string &userId, ...)
     */
    virtual void getUserRoles(const std::string &userId, StringListCallback &&cb) = 0;

    /**
     * @brief Get roles assigned to a user by internal user ID.
     * Original: IOAuth2Storage::getUserRoles(int32_t internalUserId, ...)
     */
    virtual void getUserRoles(int32_t internalUserId, StringListCallback &&cb) = 0;

    // ---------------------------------------------------------------------
    // Decision (see REPOSITORY_MAPPING.md): NO purgeExpired() here.
    //
    // Verified against all three existing IOAuth2Storage::deleteExpiredData()
    // implementations (PostgresOAuth2Storage.cc, RedisOAuth2Storage.cc,
    // MemoryOAuth2Storage.cc): none of them touch role-assignment data.
    // Postgres only sweeps oauth2_codes / oauth2_access_tokens /
    // oauth2_refresh_tokens (+ archives old tokens); Redis is a documented
    // no-op relying on key TTL; Memory only sweeps its authCodes_ /
    // accessTokens_ / refreshTokens_ maps. Role assignments have no
    // expiresAt/TTL semantic in the current model. Intentionally omitted
    // for the same reason IUserRepository omits it: a no-op purgeExpired()
    // would be boilerplate with no corresponding caller need.
    // ---------------------------------------------------------------------
};

}  // namespace oauth2
