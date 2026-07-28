#pragma once

// Task 10 (design.md §7): aggregates the four Redis oauth2 repository
// implementations behind a single construction entry point for product
// assembly code, mirroring PostgresRepositoryBundle from Task 9. This is
// ADDITIVE -- it does not replace RedisOAuth2Storage/IOAuth2Storage, which
// remain the production path wired up by OAuth2Plugin.cc today.
// Phase 1.5e: the 3 identity repos that used to live here are removed; the
// identity domain now has its own authforge::identity::* backing stores
// (see OAuth2Plugin.cc initStorage).
#include <authforge/storage/redis/RedisClientRepository.h>
#include <authforge/storage/redis/RedisGrantRepository.h>
#include <authforge/storage/redis/RedisTokenRepository.h>
#include <authforge/storage/redis/RedisConsentRepository.h>

#include <memory>
#include <string>

namespace authforge::storage::redis
{

// Task 27.5: the 4 oauth2-aggregate accessors now expose the NEW authforge::oauth2::repository::*
// interfaces the split-repos implement.
using IClientRepository = ::authforge::oauth2::repository::IClientRepository;
using IGrantRepository = ::authforge::oauth2::repository::IGrantRepository;
using ITokenRepository = ::authforge::oauth2::repository::ITokenRepository;
using IConsentRepository = ::authforge::oauth2::repository::IConsentRepository;

/**
 * @brief Aggregates all four Redis oauth2 repository implementations behind
 * a single constructor call, mirroring PostgresRepositoryBundle's
 * ergonomics (Task 9) as closely as the underlying constructor shapes
 * allow.
 *
 * Design difference from PostgresRepositoryBundle (deliberate, not an
 * oversight): PostgresRepositoryBundle has a default constructor plus a
 * separate initFromConfig(Json::Value) step, because
 * PostgresRepositoryBase::initFromConfig reads db_client_name/
 * db_client_reader keys out of a config block. RedisRepositoryBase has no
 * equivalent -- RedisOAuth2Storage's original constructor took the client
 * name directly as a plain string constructor parameter, not a config
 * block, and looked the client up immediately (no lazy init step). This
 * bundle preserves that shape: construction IS initialization, taking the
 * same optional `redisClientName` parameter RedisOAuth2Storage's constructor
 * took.
 *
 * Usage (future product assembly code, not part of Task 10's scope to wire
 * up into OAuth2Plugin.cc -- that remains on IOAuth2Storage per the task's
 * "additive, not a replacement" constraint):
 *
 *   RedisRepositoryBundle bundle("default");
 *   someService(bundle.clientRepository(), bundle.tokenRepository(), ...);
 */
class RedisRepositoryBundle
{
  public:
    explicit RedisRepositoryBundle(const std::string &redisClientName = "default");

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
    std::shared_ptr<RedisClientRepository> clientRepository_;
    std::shared_ptr<RedisGrantRepository> grantRepository_;
    std::shared_ptr<RedisTokenRepository> tokenRepository_;
    std::shared_ptr<RedisConsentRepository> consentRepository_;
};

}  // namespace authforge::storage::redis
