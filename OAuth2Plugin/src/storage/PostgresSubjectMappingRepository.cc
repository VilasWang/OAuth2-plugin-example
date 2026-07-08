#include <oauth2/storage/PostgresSubjectMappingRepository.h>
#include <drogon/drogon.h>

#include <oauth2/models/Oauth2SubjectMappings.h>

namespace oauth2
{

using namespace drogon::orm;
using namespace drogon_model::oauth2_db;

void PostgresSubjectMappingRepository::getInternalUserId(
  const std::string &subject,
  const std::string &provider,
  OptionalIntCallback &&cb
)
{
    auto sharedCb = std::make_shared<OptionalIntCallback>(std::move(cb));

    try
    {
        Mapper<Oauth2SubjectMappings> mapper(dbClientReader_);
        mapper.findOne(
          Criteria(Oauth2SubjectMappings::Cols::_provider, CompareOperator::EQ, provider) &&
            Criteria(Oauth2SubjectMappings::Cols::_subject, CompareOperator::EQ, subject),
          [sharedCb](const Oauth2SubjectMappings &mapping) {
              (*sharedCb)(mapping.getValueOfInternalUserId());
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_DEBUG << "Subject mapping not found: " << e.base().what();
              (*sharedCb)(std::nullopt);
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "getInternalUserId Exception";
        (*sharedCb)(std::nullopt);
    }
}

void PostgresSubjectMappingRepository::createSubjectMapping(
  const std::string &subject,
  int32_t internalUserId,
  const std::string &provider,
  BoolCallback &&cb
)
{
    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));

    try
    {
        Mapper<Oauth2SubjectMappings> mapper(dbClientMaster_);
        Oauth2SubjectMappings mapping;
        mapping.setSubject(subject);
        mapping.setInternalUserId(internalUserId);
        mapping.setProvider(provider);

        mapper.insert(
          mapping,
          [sharedCb](const Oauth2SubjectMappings &insertedMapping) {
              LOG_INFO << "Created subject mapping: " << insertedMapping.getValueOfProvider() << ":"
                       << insertedMapping.getValueOfSubject()
                       << " -> user_id: " << insertedMapping.getValueOfInternalUserId();
              (*sharedCb)(true);
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "Failed to create subject mapping: " << e.base().what();
              (*sharedCb)(false);
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "createSubjectMapping Exception";
        (*sharedCb)(false);
    }
}

void PostgresSubjectMappingRepository::createUserForExternalLogin(
  const std::string &externalId,
  const std::string &provider,
  OptionalIntCallback &&cb
)
{
    if (!dbClientMaster_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<OptionalIntCallback>(std::move(cb));

    // Generate a unique username from provider:externalId
    std::string username = provider + "_" + externalId.substr(0, 20);

    // Insert user with placeholder password (external auth, no local password)
    dbClientMaster_->execSqlAsync(
      "INSERT INTO users (username, password_hash, salt, email) "
      "VALUES ($1, 'EXTERNAL_AUTH_NO_PASSWORD', '', '') "
      "ON CONFLICT (username) DO UPDATE SET username = users.username "
      "RETURNING id",
      [sharedCb, provider, externalId](const drogon::orm::Result &r) {
          if (r.empty())
          {
              LOG_ERROR << "createUserForExternalLogin: no ID returned for " << provider << ":"
                        << externalId;
              (*sharedCb)(std::nullopt);
              return;
          }
          int32_t newId = r[0]["id"].as<int32_t>();
          LOG_INFO << "Created/found user for external login: " << provider << ":" << externalId
                   << " -> id=" << newId;
          (*sharedCb)(newId);
      },
      [sharedCb, provider, externalId](const drogon::orm::DrogonDbException &e) {
          LOG_ERROR << "createUserForExternalLogin failed for " << provider << ":" << externalId
                    << ": " << e.base().what();
          (*sharedCb)(std::nullopt);
      },
      username
    );
}

}  // namespace oauth2
