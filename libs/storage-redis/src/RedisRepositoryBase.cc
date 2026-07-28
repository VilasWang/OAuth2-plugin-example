#include <authforge/storage/redis/RedisRepositoryBase.h>
#include <drogon/drogon.h>

namespace authforge::storage::redis
{

RedisRepositoryBase::RedisRepositoryBase(const std::string &redisClientName)
    : redisClient_(::drogon::app().getRedisClient(redisClientName))
{
    if (redisClient_)
    {
        redisClient_->setTimeout(3.0);
        LOG_DEBUG << "RedisRepositoryBase initialized with client: " << redisClientName;
    }
    else
    {
        LOG_ERROR << "RedisRepositoryBase FAILED to get client: " << redisClientName;
    }
}

}  // namespace authforge::storage::redis
