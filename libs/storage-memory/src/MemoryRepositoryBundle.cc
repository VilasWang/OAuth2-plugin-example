#include <authforge/storage/memory/MemoryRepositoryBundle.h>

namespace authforge::storage::memory
{

MemoryRepositoryBundle::MemoryRepositoryBundle()
    : clientRepository_(std::make_shared<MemoryClientRepository>()),
      grantRepository_(std::make_shared<MemoryGrantRepository>()),
      tokenRepository_(std::make_shared<MemoryTokenRepository>()),
      consentRepository_(std::make_shared<MemoryConsentRepository>())
{
}

void MemoryRepositoryBundle::initFromConfig(const Json::Value &clientsConfig)
{
    clientRepository_->initFromConfig(clientsConfig);
}

}  // namespace authforge::storage::memory
