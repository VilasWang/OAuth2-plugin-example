#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of
// MemoryOAuth2Storage into per-aggregate implementation files, mirroring the
// Task 9 Postgres split. This one implements IRoleRepository
// (REPOSITORY_MAPPING.md #15-16) plus the admin-role-config-parsing slice of
// the original MemoryOAuth2Storage::initFromConfig(clientsConfig,
// adminConfig). It is ADDITIVE: MemoryOAuth2Storage / IOAuth2Storage are
// untouched and remain the production path used by OAuth2Plugin.cc and
// existing tests today.
//
// Physical location / namespace note (mirrors IRoleRepository.h's own header
// comment): this repository is conceptually identity-domain, but Task 10
// does not move it to libs/identity (that is M2.5 / Task 19). It stays
// under OAuth2Plugin/{include,src}/oauth2/storage/ in namespace `oauth2`,
// exactly like PostgresRoleRepository/RedisRoleRepository.
//
// State ownership (see MemoryClientRepository.h header comment for the
// general rationale): this class owns `userRoles_` -- the only map
// IRoleRepository's methods touch -- and its own private mutex.
#include <oauth2/storage/IRoleRepository.h>

#include <cstdint>
#include <json/json.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace oauth2
{

/**
 * @brief In-memory implementation of IRoleRepository.
 *
 * Faithful port of the admin-role slice of MemoryOAuth2Storage: both
 * getUserRoles() overloads (string userId direct lookup;
 * int32_t internalUserId converted to its decimal-string form before
 * lookup -- note these are DIFFERENT keys into the same userRoles_ map, an
 * existing quirk of the original code preserved as-is) default to
 * `{"user"}` when no explicit configuration exists for that key, plus the
 * admin-config-parsing half of the original initFromConfig (array-of-roles
 * per user, single-role-as-string per user, and the "no admin config at
 * all -> default admin user 'admin' with roles {admin, user}" fallback).
 */
class MemoryRoleRepository : public IRoleRepository
{
  public:
    /**
     * @brief Initialize with admin role configuration from JSON.
     *
     * Admin-role-config half of the original
     * MemoryOAuth2Storage::initFromConfig(clientsConfig, adminConfig) -- the
     * client-config half now lives in
     * MemoryClientRepository::initFromConfig(clientsConfig).
     *
     * @param adminConfig JSON object with admin user role definitions
     * (optional; if null/absent, falls back to the original's default
     * "admin" -> {"admin", "user"} behavior, exactly as
     * MemoryOAuth2Storage::initFromConfig did when its adminConfig
     * parameter defaulted to Json::Value::nullSingleton()).
     */
    void initFromConfig(const Json::Value &adminConfig = Json::Value::nullSingleton());

    void getUserRoles(const std::string &userId, StringListCallback &&cb) override;
    void getUserRoles(int32_t internalUserId, StringListCallback &&cb) override;

  private:
    std::recursive_mutex mutex_;
    std::unordered_map<std::string, std::vector<std::string>> userRoles_;
};

}  // namespace oauth2
