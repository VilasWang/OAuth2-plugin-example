#pragma once

// Task 17 slice 3 (fulla-sdk-refactor, design.md §6/§7): ports the M1
// repository interface oauth2::IClientRepository (OAuth2Plugin/include/
// oauth2/storage/IClientRepository.h) into fulla::oauth2::repository,
// depending on the freshly-ported fulla::oauth2::model::OAuth2Client
// DTO (model/Dto.h, Task 17 slice 2) instead of pulling in the old
// oauth2::OAuth2Client via IOAuth2Storage.h. This decouples the interface
// from OAuth2Plugin entirely (design.md §4.1: libs/oauth2 must not depend
// on OAuth2Plugin -- the dependency direction is the other way).
//
// Method shapes, docs, and the "no purgeExpired()" decision are carried
// over unchanged from the original (see REPOSITORY_MAPPING.md for the
// full method mapping this interface is part of). This header is
// additive: OAuth2Plugin/include/oauth2/storage/IClientRepository.h and
// its existing Memory/Redis/Postgres implementations are untouched;
// switching those implementations to depend on this new interface (so
// there is exactly one IClientRepository going forward) is a later slice.

#include <fulla/oauth2/model/Dto.h>

#include <functional>
#include <optional>
#include <string>

namespace fulla::oauth2::repository
{

/**
 * @brief Repository for OAuth2 client registration data.
 *
 * Carves out the "client" aggregate from the former god interface
 * IOAuth2Storage. See REPOSITORY_MAPPING.md for the full 30-method mapping.
 *
 * SDK consumers MUST implement this interface (design.md §7.1: client
 * lookup/validation is required for every OAuth2 flow).
 */
class IClientRepository
{
  public:
    virtual ~IClientRepository() = default;

    using ClientCallback =
      std::function<void(std::optional<fulla::oauth2::model::OAuth2Client>)>;
    using BoolCallback = std::function<void(bool)>;

    /// Get client by ID. Original: IOAuth2Storage::getClient.
    virtual void getClient(const std::string &clientId, ClientCallback &&cb) = 0;

    /// Validate client credentials. Original: IOAuth2Storage::validateClient.
    virtual void validateClient(
      const std::string &clientId,
      const std::string &clientSecret,
      BoolCallback &&cb
    ) = 0;

    // ---------------------------------------------------------------------
    // Decision (carried over from REPOSITORY_MAPPING.md): NO purgeExpired()
    // here. Client registrations do not carry an expiry/TTL semantic in
    // the current model.
    // ---------------------------------------------------------------------
};

}  // namespace fulla::oauth2::repository
