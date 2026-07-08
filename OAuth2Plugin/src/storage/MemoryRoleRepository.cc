#include <oauth2/storage/MemoryRoleRepository.h>
#include <drogon/drogon.h>

namespace oauth2
{

void MemoryRoleRepository::initFromConfig(const Json::Value &adminConfig)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Initialize admin roles from configuration
    if (!adminConfig.isNull() && adminConfig.isObject())
    {
        for (const auto &userId : adminConfig.getMemberNames())
        {
            const auto &rolesData = adminConfig[userId];
            if (rolesData.isArray())
            {
                std::vector<std::string> roles;
                for (const auto &role : rolesData)
                {
                    roles.push_back(role.asString());
                }
                userRoles_[userId] = roles;
                LOG_DEBUG << "MemoryRoleRepository: User " << userId
                          << " assigned roles: " << (roles.empty() ? 0 : roles.size());
            }
            else if (rolesData.isString())
            {
                // Single role as string
                userRoles_[userId] = {rolesData.asString()};
                LOG_DEBUG << "MemoryRoleRepository: User " << userId
                          << " assigned role: " << rolesData.asString();
            }
        }
    }
    else
    {
        // Default admin configuration for backward compatibility
        LOG_WARN << "MemoryRoleRepository: No admin configuration provided, "
                 << "using default admin user 'admin'";
        userRoles_["admin"] = {"admin", "user"};
    }
}

void MemoryRoleRepository::getUserRoles(const std::string &userId, StringListCallback &&cb)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = userRoles_.find(userId);
    if (it != userRoles_.end())
    {
        cb(it->second);
    }
    else
    {
        // Default to regular user role if no specific configuration
        cb({"user"});
    }
}

void MemoryRoleRepository::getUserRoles(int32_t internalUserId, StringListCallback &&cb)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string userIdStr = std::to_string(internalUserId);
    auto it = userRoles_.find(userIdStr);
    if (it != userRoles_.end())
    {
        cb(it->second);
    }
    else
    {
        // Default to regular user role if no specific configuration
        cb({"user"});
    }
}

}  // namespace oauth2
