#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of
// MemoryOAuth2Storage into per-aggregate implementation files, mirroring the
// Task 9 Postgres split. This one implements IConsentRepository
// (REPOSITORY_MAPPING.md #26-28). It is ADDITIVE: MemoryOAuth2Storage /
// IOAuth2Storage are untouched and remain the production path used by
// OAuth2Plugin.cc and existing tests today.
//
// State ownership (see MemoryClientRepository.h header comment for the
// general rationale): this class owns `userConsents_` -- the only map
// IConsentRepository's methods touch -- and its own private mutex.
#include <oauth2/storage/IConsentRepository.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace oauth2
{

/**
 * @brief In-memory implementation of IConsentRepository.
 *
 * F4 (design.md §7.2): the interface takes UserRef instead of a bare
 * int32_t internalUserId. Per UserRef.h's documented contract, ONLY
 * storage-layer implementations like this one are allowed to unwrap
 * `user.internalUserId` to build the map key -- that unwrap happens here
 * (once per method), then the key/map logic itself is byte-for-byte the
 * same as MemoryOAuth2Storage's original
 * hasUserConsent/saveUserConsent/revokeUserConsent.
 */
class MemoryConsentRepository : public IConsentRepository
{
  public:
    void hasUserConsent(
      const UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) override;

    void saveUserConsent(
      const UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) override;

    void revokeUserConsent(
      const UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      VoidCallback &&cb
    ) override;

  private:
    std::recursive_mutex mutex_;
    // User consents: "user_id:client_id:scope" -> timestamp
    std::unordered_map<std::string, int64_t> userConsents_;

    int64_t getCurrentTimestamp() const;
};

}  // namespace oauth2
