#pragma once

#include <functional>
#include <optional>
#include <string>
#include <cstdint>
#include <json/json.h>

namespace authforge::identity
{

/**
 * @brief User data from repository
 */
struct UserData
{
    int64_t id;
    std::string username;
    std::string email;
    std::string passwordHash;
    std::string salt;
    std::string publicSub;
    bool emailVerified = false;
    bool mfaEnabled = false;
    int64_t lockedUntil = 0;
    int failedLoginCount = 0;
};

/**
 * @brief Repository interface for user management
 */
class IUserRepository
{
public:
    virtual ~IUserRepository() = default;

    /**
     * @brief Find user by email (normalized)
     */
    virtual void findByEmail(
      const std::string &email,
      std::function<void(std::optional<UserData>)> &&callback
    ) = 0;

    /**
     * @brief Find user by username
     */
    virtual void findByUsername(
      const std::string &username,
      std::function<void(std::optional<UserData>)> &&callback
    ) = 0;

    /**
     * @brief Find user by internal ID
     */
    virtual void findById(
      int64_t userId,
      std::function<void(std::optional<UserData>)> &&callback
    ) = 0;

    /**
     * @brief Create a new user
     * @param userData User data to create
     * @param callback Returns user ID on success, nullopt on failure
     */
    virtual void create(
      const UserData &userData,
      std::function<void(std::optional<int64_t>)> &&callback
    ) = 0;

    /**
     * @brief Update user's password hash
     */
    virtual void updatePasswordHash(
      int64_t userId,
      const std::string &newHash,
      std::function<void(bool)> &&callback
    ) = 0;

    /**
     * @brief Reset failed login count
     */
    virtual void resetFailedLogins(
      int64_t userId,
      std::function<void(bool)> &&callback
    ) = 0;

    /**
     * @brief Increment failed login count and optionally lock account
     */
    virtual void incrementFailedLogins(
      int64_t userId,
      std::function<void(bool)> &&callback
    ) = 0;

    /**
     * @brief Get user info with roles (for userinfo endpoint)
     */
    virtual void getUserInfoWithRoles(
      int64_t userId,
      std::function<void(std::optional<Json::Value>)> &&callback
    ) = 0;
};

}  // namespace authforge::identity
