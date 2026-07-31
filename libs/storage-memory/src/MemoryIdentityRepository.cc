#include <authforge/storage/memory/MemoryIdentityRepository.h>

#include <drogon/drogon.h>

#include <string>

namespace authforge::storage::memory
{

using authforge::identity::UserData;
using RolesCallback = authforge::identity::IRoleRepository::RolesCallback;
using OptionalIntCallback = authforge::identity::ISubjectMappingRepository::OptionalIntCallback;
using BoolCallback = authforge::identity::ISubjectMappingRepository::BoolCallback;

void MemoryIdentityRepository::initAdminRoles(const Json::Value &adminConfig)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!adminConfig.isNull() && adminConfig.isObject())
    {
        for (const auto &userId : adminConfig.getMemberNames())
        {
            const auto &rolesData = adminConfig[userId];
            if (rolesData.isArray())
            {
                std::vector<std::string> roles;
                for (const auto &role : rolesData)
                    roles.push_back(role.asString());
                userRoles_[userId] = roles;
            }
            else if (rolesData.isString())
            {
                userRoles_[userId] = {rolesData.asString()};
            }
        }
    }
    else
    {
        LOG_WARN << "MemoryIdentityRepository: no admin configuration, defaulting 'admin' user";
        userRoles_["admin"] = {"admin", "user"};
    }
}

void MemoryIdentityRepository::getRoles(int32_t internalUserId, RolesCallback &&cb)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Legacy quirk preserved: the int-id path converts to a string key (the
    // config admin_users block is keyed by subject string, e.g. "admin").
    auto it = userRoles_.find(std::to_string(internalUserId));
    cb(it == userRoles_.end() ? std::vector<std::string>{"user"} : it->second);
}

void MemoryIdentityRepository::getRoles(const std::string &subject, RolesCallback &&cb)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = userRoles_.find(subject);
    cb(it == userRoles_.end() ? std::vector<std::string>{"user"} : it->second);
}

void MemoryIdentityRepository::getInternalUserId(
  const std::string &subject,
  const std::string &provider,
  OptionalIntCallback &&cb
)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = subjectMappings_.find(provider + ":" + subject);
    cb(it == subjectMappings_.end() ? std::nullopt : std::optional<int32_t>(it->second));
}

void MemoryIdentityRepository::createSubjectMapping(
  const std::string &subject,
  int32_t internalUserId,
  const std::string &provider,
  BoolCallback &&cb
)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    subjectMappings_[provider + ":" + subject] = internalUserId;
    cb(true);
}

UserData MemoryIdentityRepository::syntheticUser(int32_t id) const
{
    UserData data;
    data.id = id;
    data.username = "user_" + std::to_string(id);
    data.email = "user_" + std::to_string(id) + "@example.com";
    return data;
}

// --- IUserRepository placeholder impls (memory backend has no real user store;
// mirrors the legacy MemoryUserRepository which synthesized JSON on the fly) ---

void MemoryIdentityRepository::findById(
  int32_t userId,
  std::function<void(std::optional<UserData>)> &&callback
)
{
    callback(syntheticUser(userId));
}

void MemoryIdentityRepository::findByPublicSub(
  const std::string &publicSub,
  std::function<void(std::optional<UserData>)> &&callback
)
{
    try
    {
        callback(syntheticUser(std::stoi(publicSub)));
    }
    catch (...)
    {
        callback(std::nullopt);
    }
}

void MemoryIdentityRepository::findByEmail(
  const std::string &email,
  std::function<void(std::optional<UserData>)> &&callback
)
{
    callback(std::nullopt);
}

void MemoryIdentityRepository::findByUsername(
  const std::string &username,
  std::function<void(std::optional<UserData>)> &&callback
)
{
    callback(std::nullopt);
}

void MemoryIdentityRepository::create(
  const UserData &userData,
  std::function<void(std::optional<int32_t>, std::string)> &&callback
)
{
    // Memory backend cannot persist; report unsupported (callers fall back to
    // the configured DB-backed impl in production).
    callback(std::nullopt, "");
}

void MemoryIdentityRepository::updatePasswordHash(
  int32_t userId,
  const std::string &newHash,
  std::function<void(bool)> &&callback
)
{
    callback(false);
}

void MemoryIdentityRepository::resetFailedLogins(
  int32_t userId,
  std::function<void(bool)> &&callback
)
{
    callback(true);
}

void MemoryIdentityRepository::incrementFailedLogins(
  int32_t userId,
  std::function<void(bool)> &&callback
)
{
    callback(true);
}

void MemoryIdentityRepository::getUserInfoWithRoles(
  int32_t userId,
  std::function<void(std::optional<Json::Value>)> &&callback
)
{
    Json::Value info;
    info["sub"] = std::to_string(userId);
    info["name"] = "user_" + std::to_string(userId);
    info["email"] = "user_" + std::to_string(userId) + "@example.com";
    std::vector<std::string> roles;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = userRoles_.find(std::to_string(userId));
        if (it != userRoles_.end())
            roles = it->second;
    }
    if (roles.empty())
        roles = {"user"};
    Json::Value roleArray(Json::arrayValue);
    for (const auto &r : roles)
        roleArray.append(r);
    info["roles"] = roleArray;
    callback(info);
}

}  // namespace authforge::storage::memory
