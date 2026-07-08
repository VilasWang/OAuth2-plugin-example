#include <oauth2/storage/RedisUserRepository.h>

namespace oauth2
{

void RedisUserRepository::getUserInfo(const std::string &userId, OptionalJsonCallback &&cb)
{
    // Redis storage doesn't maintain user details
    // Return nullopt to indicate user info not available
    cb(std::nullopt);
}

void RedisUserRepository::getUserInfo(int32_t internalUserId, OptionalJsonCallback &&cb)
{
    // Redis storage doesn't maintain user details
    // Return nullopt to indicate user info not available
    cb(std::nullopt);
}

}  // namespace oauth2
