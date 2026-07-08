#include <oauth2/storage/PostgresRepositoryBundle.h>

namespace oauth2
{

PostgresRepositoryBundle::PostgresRepositoryBundle()
    : clientRepository_(std::make_shared<PostgresClientRepository>()),
      grantRepository_(std::make_shared<PostgresGrantRepository>()),
      tokenRepository_(std::make_shared<PostgresTokenRepository>()),
      consentRepository_(std::make_shared<PostgresConsentRepository>()),
      userRepository_(std::make_shared<PostgresUserRepository>()),
      roleRepository_(std::make_shared<PostgresRoleRepository>()),
      subjectMappingRepository_(std::make_shared<PostgresSubjectMappingRepository>())
{
}

void PostgresRepositoryBundle::initFromConfig(const Json::Value &config)
{
    clientRepository_->initFromConfig(config);
    grantRepository_->initFromConfig(config);
    tokenRepository_->initFromConfig(config);
    consentRepository_->initFromConfig(config);
    userRepository_->initFromConfig(config);
    roleRepository_->initFromConfig(config);
    subjectMappingRepository_->initFromConfig(config);
}

}  // namespace oauth2
