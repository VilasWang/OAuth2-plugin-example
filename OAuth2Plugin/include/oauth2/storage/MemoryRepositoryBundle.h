#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): aggregates the seven
// Memory repository implementations (4 oauth2 + 3 identity, per Task 7/8)
// behind a single construction/initialization entry point for product
// assembly code, mirroring PostgresRepositoryBundle (Task 9) and
// RedisRepositoryBundle (Task 10). This is ADDITIVE -- it does not replace
// MemoryOAuth2Storage/IOAuth2Storage, which remain the production path
// wired up by OAuth2Plugin.cc and existing tests today.
#include <authforge/storage/memory/MemoryClientRepository.h>
#include <authforge/storage/memory/MemoryGrantRepository.h>
#include <authforge/storage/memory/MemoryTokenRepository.h>
#include <authforge/storage/memory/MemoryConsentRepository.h>
#include <oauth2/storage/MemoryUserRepository.h>
#include <oauth2/storage/MemoryRoleRepository.h>
#include <oauth2/storage/MemorySubjectMappingRepository.h>

#include <json/json.h>
#include <memory>

namespace oauth2
{

// Task 27.5 (authforge-sdk-refactor): the 4 oauth2-aggregate accessors below
// now expose the NEW Domain-layer repository interfaces
// (authforge::oauth2::repository::*) that the Memory split-repos implement.
// The 3 identity accessors (User/Role/SubjectMapping) still return the
// legacy oauth2::* interfaces -- their migration to authforge::identity::* is
// a separate follow-up (identity-side), out of this task's oauth2 scope.
using IClientRepository = ::authforge::oauth2::repository::IClientRepository;
using IGrantRepository = ::authforge::oauth2::repository::IGrantRepository;
using ITokenRepository = ::authforge::oauth2::repository::ITokenRepository;
using IConsentRepository = ::authforge::oauth2::repository::IConsentRepository;

/**
 * @brief Aggregates all seven Memory repository implementations behind a
 * single initFromConfig() call, mirroring
 * MemoryOAuth2Storage::initFromConfig(clientsConfig, adminConfig)'s
 * two-parameter shape -- but now dispatching each half to the repository
 * that actually owns the corresponding state (client config ->
 * ::authforge::storage::memory::MemoryClientRepository, admin/role config -> MemoryRoleRepository;
 * see REPOSITORY_MAPPING.md and each repository's own header comment for the split rationale).
 *
 * Usage (future product assembly code, not part of Task 10's scope to wire
 * up into OAuth2Plugin.cc -- that remains on IOAuth2Storage per the task's
 * "additive, not a replacement" constraint):
 *
 *   MemoryRepositoryBundle bundle;
 *   bundle.initFromConfig(clientsConfig, adminConfig);
 *   someService(bundle.clientRepository(), bundle.tokenRepository(), ...);
 */
class MemoryRepositoryBundle
{
  public:
    MemoryRepositoryBundle();

    /**
     * @brief Initialize client + admin-role state, mirroring
     * MemoryOAuth2Storage::initFromConfig(clientsConfig, adminConfig)'s
     * signature and defaulting exactly.
     */
    void initFromConfig(
      const Json::Value &clientsConfig,
      const Json::Value &adminConfig = Json::Value::nullSingleton()
    );

    std::shared_ptr<IClientRepository> clientRepository() const
    {
        return clientRepository_;
    }

    std::shared_ptr<IGrantRepository> grantRepository() const
    {
        return grantRepository_;
    }

    std::shared_ptr<ITokenRepository> tokenRepository() const
    {
        return tokenRepository_;
    }

    std::shared_ptr<IConsentRepository> consentRepository() const
    {
        return consentRepository_;
    }

    std::shared_ptr<::oauth2::IUserRepository> userRepository() const
    {
        return userRepository_;
    }

    std::shared_ptr<::oauth2::IRoleRepository> roleRepository() const
    {
        return roleRepository_;
    }

    std::shared_ptr<::oauth2::ISubjectMappingRepository> subjectMappingRepository() const
    {
        return subjectMappingRepository_;
    }

  private:
    std::shared_ptr<::authforge::storage::memory::MemoryClientRepository> clientRepository_;
    std::shared_ptr<::authforge::storage::memory::MemoryGrantRepository> grantRepository_;
    std::shared_ptr<::authforge::storage::memory::MemoryTokenRepository> tokenRepository_;
    std::shared_ptr<::authforge::storage::memory::MemoryConsentRepository> consentRepository_;
    std::shared_ptr<MemoryUserRepository> userRepository_;
    std::shared_ptr<MemoryRoleRepository> roleRepository_;
    std::shared_ptr<MemorySubjectMappingRepository> subjectMappingRepository_;
};

}  // namespace oauth2
