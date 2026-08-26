#pragma once

#ifdef WITH_SOCIAL

// #71: Redis-backed implementation of the identity-layer link-state port.
// See ISocialLinkStateStore.h for the flow contract and the .cc for the
// exact Redis commands (SET NX EX / GETDEL).

#include <fulla/common/ports/ICryptoProvider.h>
#include <fulla/identity/ISocialLinkStateStore.h>

#include <drogon/nosql/RedisClient.h>

#include <memory>

namespace fulla::drogon::adapters
{

class RedisSocialLinkStateStore : public fulla::identity::ISocialLinkStateStore
{
  public:
    /// `redisClient`/`cryptoProvider` may be null (store unavailable ->
    /// fail-closed nullopt results). `ttlSeconds` defaults to 600.
    RedisSocialLinkStateStore(
      ::drogon::nosql::RedisClientPtr redisClient,
      std::shared_ptr<fulla::common::ports::ICryptoProvider> cryptoProvider,
      int ttlSeconds = 600
    );

    void issue(int32_t internalUserId, const std::string &provider, IssueCallback &&cb) override;
    void consume(const std::string &state, ConsumeCallback &&cb) override;

  private:
    ::drogon::nosql::RedisClientPtr redisClient_;
    std::shared_ptr<fulla::common::ports::ICryptoProvider> cryptoProvider_;
    int ttlSeconds_;
};

}  // namespace fulla::drogon::adapters

#endif  // WITH_SOCIAL
