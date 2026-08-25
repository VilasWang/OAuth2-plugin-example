#include <fulla/storage/redis/RedisRepositoryBundle.h>

namespace fulla::storage::redis
{

RedisRepositoryBundle::RedisRepositoryBundle(const std::string &redisClientName)
    : clientRepository_(std::make_shared<RedisClientRepository>(redisClientName)),
      grantRepository_(std::make_shared<RedisGrantRepository>(redisClientName)),
      tokenRepository_(std::make_shared<RedisTokenRepository>(redisClientName)),
      consentRepository_(std::make_shared<RedisConsentRepository>(redisClientName))
{
}

}  // namespace fulla::storage::redis
