#include <oauth2/storage/RedisRepositoryBundle.h>

namespace oauth2
{

RedisRepositoryBundle::RedisRepositoryBundle(const std::string &redisClientName)
    : clientRepository_(std::make_shared<RedisClientRepository>(redisClientName)),
      grantRepository_(std::make_shared<RedisGrantRepository>(redisClientName)),
      tokenRepository_(std::make_shared<RedisTokenRepository>(redisClientName)),
      consentRepository_(std::make_shared<RedisConsentRepository>(redisClientName)),
      userRepository_(std::make_shared<RedisUserRepository>(redisClientName)),
      roleRepository_(std::make_shared<RedisRoleRepository>(redisClientName)),
      subjectMappingRepository_(
        std::make_shared<RedisSubjectMappingRepository>(redisClientName)
      )
{
}

}  // namespace oauth2
