#include <oauth2/storage/MemoryRepositoryBundle.h>

namespace oauth2
{

MemoryRepositoryBundle::MemoryRepositoryBundle()
    : clientRepository_(std::make_shared<MemoryClientRepository>()),
      grantRepository_(std::make_shared<MemoryGrantRepository>()),
      tokenRepository_(std::make_shared<MemoryTokenRepository>()),
      consentRepository_(std::make_shared<MemoryConsentRepository>()),
      userRepository_(std::make_shared<MemoryUserRepository>()),
      roleRepository_(std::make_shared<MemoryRoleRepository>()),
      subjectMappingRepository_(std::make_shared<MemorySubjectMappingRepository>())
{
}

void MemoryRepositoryBundle::initFromConfig(
  const Json::Value &clientsConfig,
  const Json::Value &adminConfig
)
{
    clientRepository_->initFromConfig(clientsConfig);
    roleRepository_->initFromConfig(adminConfig);
}

}  // namespace oauth2
