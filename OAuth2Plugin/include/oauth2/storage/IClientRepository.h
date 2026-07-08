#pragma once

// This header is part of the M1 storage interface split (design.md §7).
// It carves the "client" aggregate out of the IOAuth2Storage god interface
// WITHOUT moving the directory (that happens in M2b / Task 17). It is
// additive: IOAuth2Storage.h is untouched and existing implementations keep
// satisfying it until Task 9/10 migrate them onto these new interfaces.
//
// IOAuth2Storage.h is reused here for the OAuth2Client DTO to avoid a
// duplicate/competing definition. See REPOSITORY_MAPPING.md for the full
// method mapping and the rationale for this include.
#include <oauth2/storage/IOAuth2Storage.h>

#include <functional>
#include <optional>
#include <string>

namespace oauth2
{

/**
 * @brief Repository for OAuth2 client registration data.
 *
 * Carves out the "client" aggregate from the former god interface
 * IOAuth2Storage. See REPOSITORY_MAPPING.md for the full 30-method mapping.
 *
 * SDK consumers MUST implement this interface (design.md §7.1: "SDK 消费者
 * 必须实现？" = 是) since client lookup/validation is required for every
 * OAuth2 flow.
 */
class IClientRepository
{
  public:
    virtual ~IClientRepository() = default;

    using ClientCallback = std::function<void(std::optional<OAuth2Client>)>;
    using BoolCallback = std::function<void(bool)>;

    /**
     * @brief Get client by ID.
     * Original: IOAuth2Storage::getClient
     */
    virtual void getClient(const std::string &clientId, ClientCallback &&cb) = 0;

    /**
     * @brief Validate client credentials.
     * Original: IOAuth2Storage::validateClient
     */
    virtual void validateClient(
      const std::string &clientId,
      const std::string &clientSecret,
      BoolCallback &&cb
    ) = 0;

    // ---------------------------------------------------------------------
    // Decision (see REPOSITORY_MAPPING.md): NO purgeExpired() here.
    //
    // Client registrations do not carry an expiry/TTL semantic in the
    // current model (OAuth2Client has no expiresAt field, unlike auth
    // codes/tokens). IOAuth2Storage::deleteExpiredData() never purged
    // clients in any of the three existing implementations. Forcing a
    // no-op purgeExpired() onto every IClientRepository implementation
    // would be a pure-boilerplate requirement with no corresponding
    // caller need, so it is intentionally omitted. If a future need for
    // client expiry emerges, add it then with real semantics.
    // ---------------------------------------------------------------------
};

}  // namespace oauth2
