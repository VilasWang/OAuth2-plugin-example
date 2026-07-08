#include <oauth2/storage/MemoryUserRepository.h>
#include <string>

namespace oauth2
{

void MemoryUserRepository::getUserInfo(const std::string &userId, OptionalJsonCallback &&cb)
{
    // For memory storage, return basic user info
    // In a real implementation, this would query a user database
    try
    {
        int32_t numericUserId = std::stoi(userId);
        getUserInfo(numericUserId, std::move(cb));
        return;
    }
    catch (...)
    {
        // Not a numeric ID, return nullopt
        cb(std::nullopt);
    }
}

void MemoryUserRepository::getUserInfo(int32_t internalUserId, OptionalJsonCallback &&cb)
{
    // For memory storage, return basic user info
    // In production, this would query the users table
    Json::Value userInfo;
    userInfo["id"] = internalUserId;
    userInfo["username"] = "user_" + std::to_string(internalUserId);
    userInfo["name"] = "user_" + std::to_string(internalUserId);
    userInfo["email"] = "user_" + std::to_string(internalUserId) + "@example.com";

    cb(userInfo);
}

}  // namespace oauth2
