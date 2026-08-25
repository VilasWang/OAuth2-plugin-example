#include <fulla/storage/postgres/PostgresRepositoryBundle.h>

namespace fulla::storage::postgres
{

PostgresRepositoryBundle::PostgresRepositoryBundle()
    : clientRepository_(std::make_shared<PostgresClientRepository>()),
      grantRepository_(std::make_shared<PostgresGrantRepository>()),
      tokenRepository_(std::make_shared<PostgresTokenRepository>()),
      consentRepository_(std::make_shared<PostgresConsentRepository>())
{
}

void PostgresRepositoryBundle::initFromConfig(const Json::Value &config)
{
    clientRepository_->initFromConfig(config);
    grantRepository_->initFromConfig(config);
    tokenRepository_->initFromConfig(config);
    consentRepository_->initFromConfig(config);
}

}  // namespace fulla::storage::postgres
