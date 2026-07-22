#pragma once

// M1 storage interface split (design.md §7). See IClientRepository.h header
// comment for the general rationale (additive, non-migrating, see
// REPOSITORY_MAPPING.md for the full mapping).
//
// IMPORTANT -- physical location vs. domain ownership (Task 8 scope note):
// this interface is conceptually part of the *identity* domain (design.md
// §7.1: "IUserRepository ... 归属包 identity"), but Task 8 does NOT move it
// into libs/identity or the authforge::identity namespace. That directory
// and namespace migration is M2.5 / Task 19. For now this header stays
// physically under OAuth2Plugin/include/oauth2/storage/ and in namespace
// `oauth2`, exactly like the M1 oauth2 repositories from Task 7, purely to
// carve the method group out of IOAuth2Storage without a big-bang move.
// Do not treat this file's current location/namespace as "identity has
// already been extracted" -- it has not; that is future work.
//
// IOAuth2Storage.h is reused here for the OptionalJsonCallback alias (it is
// declared as a public nested `using` inside IOAuth2Storage, so it is
// reachable via the qualified name `IOAuth2Storage::OptionalJsonCallback`
// from outside the class -- verified by mirroring the same pattern
// IGrantRepository.h already uses for `IOAuth2Storage::AuthorizationTransaction`).
// This avoids defining a second, competing `std::function<...>` alias with
// the same shape.
#include <oauth2/storage/StorageCallbacks.h>

#include <cstdint>
#include <string>

namespace oauth2
{

/**
 * @brief Repository for user profile / userinfo data -- the "user"
 * aggregate.
 *
 * Carves out the getUserInfo() method group from the former god interface
 * IOAuth2Storage. See REPOSITORY_MAPPING.md for the full 32-method mapping
 * (entries #17-18).
 *
 * Domain ownership (design.md §7.1): this belongs to the identity SDK
 * package once libs/identity exists (M2.5, Task 19); see the file header
 * comment above for why it is not physically there yet.
 *
 * SDK consumers MAY implement this interface (design.md §7.1: identity
 * repositories are "可选（不集成身份则不实现）") -- a deployment that does
 * not integrate identity/userinfo lookups can skip it.
 */
class IUserRepository
{
  public:
    virtual ~IUserRepository() = default;

    // A3: OptionalJsonCallback now defined standalone in StorageCallbacks.h

    /**
     * @brief Get user information by external user ID (string form).
     * Original: IOAuth2Storage::getUserInfo(const std::string &userId, ...)
     */
    virtual void getUserInfo(const std::string &userId, OptionalJsonCallback &&cb) = 0;

    /**
     * @brief Get user information by internal user ID.
     * Original: IOAuth2Storage::getUserInfo(int32_t internalUserId, ...)
     */
    virtual void getUserInfo(int32_t internalUserId, OptionalJsonCallback &&cb) = 0;

    // ---------------------------------------------------------------------
    // Decision (see REPOSITORY_MAPPING.md): NO purgeExpired() here.
    //
    // User records have no expiresAt/TTL semantic in the current model,
    // and IOAuth2Storage::deleteExpiredData() never purged user rows in
    // any of the three existing implementations. Intentionally omitted for
    // the same reason IClientRepository omits it: a no-op purgeExpired()
    // would be boilerplate with no corresponding caller need.
    // ---------------------------------------------------------------------
};

}  // namespace oauth2
