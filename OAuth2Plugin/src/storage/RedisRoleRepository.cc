#include <oauth2/storage/RedisRoleRepository.h>

namespace oauth2
{

void RedisRoleRepository::getUserRoles(const std::string &userId, StringListCallback &&cb)
{
    // Default role for redis (until we implement role storage in redis)
    cb({"user"});
}

void RedisRoleRepository::getUserRoles(int32_t internalUserId, StringListCallback &&cb)
{
    // Default role for redis (until we implement role storage in redis)
    cb({"user"});
}

}  // namespace oauth2
