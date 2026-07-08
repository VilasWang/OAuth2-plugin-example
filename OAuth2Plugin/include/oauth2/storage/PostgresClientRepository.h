#pragma once

// Task 9 (design.md §7 / REPOSITORY_MAPPING.md): split of PostgresOAuth2Storage
// into per-aggregate implementation files. This one implements
// IClientRepository (methods #1-2 of REPOSITORY_MAPPING.md: getClient,
// validateClient). It is ADDITIVE: PostgresOAuth2Storage / IOAuth2Storage are
// untouched and remain the production path used by OAuth2Plugin.cc today.
#include <oauth2/storage/IClientRepository.h>
#include <oauth2/storage/PostgresRepositoryBase.h>

#include <memory>

namespace oauth2
{

/**
 * @brief PostgreSQL implementation of IClientRepository.
 *
 * Lifetime safety (preserved from PostgresOAuth2Storage, "defect 1.8"
 * pattern): inherits std::enable_shared_from_this<PostgresClientRepository>
 * so async continuations can capture `auto self = shared_from_this();` and
 * keep this object alive until in-flight DB callbacks complete. getClient()
 * uses this pattern (its scope-fetch continuation captures `self`), mirroring
 * the original PostgresOAuth2Storage::getClient.
 */
class PostgresClientRepository : public IClientRepository,
                                 public PostgresRepositoryBase,
                                 public std::enable_shared_from_this<PostgresClientRepository>
{
  public:
    PostgresClientRepository() = default;

    void getClient(const std::string &clientId, ClientCallback &&cb) override;
    void validateClient(
      const std::string &clientId,
      const std::string &clientSecret,
      BoolCallback &&cb
    ) override;
};

}  // namespace oauth2
