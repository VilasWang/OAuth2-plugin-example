#include <oauth2/storage/MemoryRepositoryBundle.h>

namespace oauth2
{

MemoryRepositoryBundle::MemoryRepositoryBundle()
    : clientRepository_(std::make_shared<::authforge::storage::memory::MemoryClientRepository>()),
      grantRepository_(std::make_shared<::authforge::storage::memory::MemoryGrantRepository>()),
      tokenRepository_(std::make_shared<::authforge::storage::memory::MemoryTokenRepository>()),
      consentRepository_(std::make_shared<::authforge::storage::memory::MemoryConsentRepository>())
{
}

void MemoryRepositoryBundle::initFromConfig(const Json::Value &clientsConfig)
{
    clientRepository_->initFromConfig(clientsConfig);
}

}  // namespace oauth2
