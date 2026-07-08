#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of
// MemoryOAuth2Storage into per-aggregate implementation files, mirroring the
// Task 9 Postgres split. This one implements IClientRepository
// (REPOSITORY_MAPPING.md #1-2: getClient, validateClient) plus the
// client-config-parsing slice of the original
// MemoryOAuth2Storage::initFromConfig(clientsConfig, adminConfig). It is
// ADDITIVE: MemoryOAuth2Storage / IOAuth2Storage are untouched and remain
// the production path used by OAuth2Plugin.cc and existing tests today.
//
// State ownership decision (Task 10 scope note): the pre-split
// MemoryOAuth2Storage held ALL state (clients_, authCodes_, accessTokens_,
// refreshTokens_, userRoles_, subjectMappings_, transactions_,
// userConsents_) behind one shared std::recursive_mutex, because it was one
// class implementing one god interface. Splitting into seven per-aggregate
// classes means each class now owns only the map(s) its own interface
// methods touch -- IClientRepository's methods only ever read/write
// `clients_`, so this class holds `clients_` and its own private mutex, not
// a shared one. There is no cross-repository state sharing need: every
// IClientRepository method is self-contained against `clients_` alone (see
// REPOSITORY_MAPPING.md "给 Task 9/10/11/19 的衔接提示" and the task
// instructions for Task 10).
//
// Lifetime/threading note (mirrors the original MemoryOAuth2Storage, NOT
// the Postgres/Redis split classes): this class does NOT inherit
// std::enable_shared_from_this<>. The original MemoryOAuth2Storage never
// did either -- its callbacks are invoked synchronously/inline while still
// holding the mutex (no async continuation crosses an event-loop
// boundary), so there is no "keep the object alive until an in-flight
// callback fires" concern the way there is for
// Postgres/Redis (whose callbacks run later, on a DB/Redis client thread).
#include <oauth2/storage/IClientRepository.h>

#include <json/json.h>
#include <mutex>
#include <string>
#include <unordered_map>

namespace oauth2
{

/**
 * @brief In-memory implementation of IClientRepository.
 *
 * Faithful port of the client-registration slice of MemoryOAuth2Storage:
 * getClient/validateClient logic (including the constant-time secret
 * comparison and PUBLIC-client secret-skip rule) and the client-parsing half
 * of the original initFromConfig (redirect_uri / allowed_scopes
 * single-or-array handling, the "vue-client" default-scopes backward
 * compatibility branch, and the client_type parsing with CONFIDENTIAL
 * fallback on invalid values).
 */
class MemoryClientRepository : public IClientRepository
{
  public:
    /**
     * @brief Initialize with client configuration from JSON.
     *
     * Client-config half of the original
     * MemoryOAuth2Storage::initFromConfig(clientsConfig, adminConfig) --
     * the admin-role half now lives in
     * MemoryRoleRepository::initFromConfig(adminConfig).
     *
     * @param clientsConfig JSON object with client definitions
     */
    void initFromConfig(const Json::Value &clientsConfig);

    void getClient(const std::string &clientId, ClientCallback &&cb) override;
    void validateClient(
      const std::string &clientId,
      const std::string &clientSecret,
      BoolCallback &&cb
    ) override;

  private:
    std::recursive_mutex mutex_;
    std::unordered_map<std::string, OAuth2Client> clients_;
};

}  // namespace oauth2
