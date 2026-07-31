#pragma once

// Task 10 (design.md §7): aggregates the four Memory oauth2 repository
// implementations behind a single construction/initialization entry point
// for product assembly code, mirroring PostgresRepositoryBundle (Task 9)
// and RedisRepositoryBundle (Task 10).
// Phase 1.5e: the 3 identity repos that used to live here are removed; the
// identity domain now has its own authforge::identity::* backing store
// (MemoryIdentityRepository), constructed separately in OAuth2Plugin.cc
// initStorage. Accordingly initFromConfig() takes only the clients config
// (the admin_users block is consumed by MemoryIdentityRepository::initAdminRoles).
// Phase 2: relocated from OAuth2Plugin/storage/ into this package.
#include <authforge/storage/memory/MemoryClientRepository.h>
#include <authforge/storage/memory/MemoryGrantRepository.h>
#include <authforge/storage/memory/MemoryTokenRepository.h>
#include <authforge/storage/memory/MemoryConsentRepository.h>

#include <json/json.h>
#include <memory>

namespace authforge::storage::memory
{

// The 4 oauth2-aggregate accessors expose the NEW Domain-layer repository
// interfaces (authforge::oauth2::repository::*) that the Memory split-repos
// implement.
using IClientRepository = ::authforge::oauth2::repository::IClientRepository;
using IGrantRepository = ::authforge::oauth2::repository::IGrantRepository;
using ITokenRepository = ::authforge::oauth2::repository::ITokenRepository;
using IConsentRepository = ::authforge::oauth2::repository::IConsentRepository;

/**
 * @brief Aggregates all four Memory oauth2 repository implementations
 * behind a single initFromConfig() call.
 *
 *   MemoryRepositoryBundle bundle;
 *   bundle.initFromConfig(clientsConfig);
 *   someService(bundle.clientRepository(), bundle.tokenRepository(), ...);
 */
class MemoryRepositoryBundle
{
  public:
    MemoryRepositoryBundle();

    /**
     * @brief Initialize client state from the config.json "clients" block.
     * The admin_users block is no longer consumed here -- it is owned by
     * MemoryIdentityRepository::initAdminRoles (Phase 1.5d/e).
     */
    void initFromConfig(const Json::Value &clientsConfig);

    std::shared_ptr<IClientRepository> clientRepository() const
    {
        return clientRepository_;
    }

    std::shared_ptr<IGrantRepository> grantRepository() const
    {
        return grantRepository_;
    }

    std::shared_ptr<ITokenRepository> tokenRepository() const
    {
        return tokenRepository_;
    }

    std::shared_ptr<IConsentRepository> consentRepository() const
    {
        return consentRepository_;
    }

  private:
    std::shared_ptr<MemoryClientRepository> clientRepository_;
    std::shared_ptr<MemoryGrantRepository> grantRepository_;
    std::shared_ptr<MemoryTokenRepository> tokenRepository_;
    std::shared_ptr<MemoryConsentRepository> consentRepository_;
};

}  // namespace authforge::storage::memory
