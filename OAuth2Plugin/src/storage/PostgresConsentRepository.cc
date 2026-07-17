#include <oauth2/storage/PostgresConsentRepository.h>
#include <drogon/drogon.h>

#include <authforge/storage/postgres/models/Oauth2UserConsents.h>

namespace oauth2
{

// Task 27.5: callback + DTO aliases for the new base interface; safe at namespace scope here (this
// .cc does not include IOAuth2Storage.h, so no oauth2::* clash).
using UserRef = ::authforge::oauth2::model::UserRef;
using BoolCallback = IConsentRepositoryBase::BoolCallback;
using VoidCallback = IConsentRepositoryBase::VoidCallback;

using namespace drogon::orm;
using namespace drogon_model::oauth2_db;

void PostgresConsentRepository::hasUserConsent(
  const UserRef &user,
  const std::string &clientId,
  const std::string &scope,
  BoolCallback &&cb
)
{
    // F4: unwrap the opaque UserRef to the internal key the query needs.
    // See UserRef.h -- this is the one place (storage-layer implementation)
    // permitted to do so.
    int32_t internalUserId = user.internalUserId;

    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));

    try
    {
        Mapper<Oauth2UserConsents> mapper(dbClientReader_);
        mapper.findBy(
          Criteria(
            Oauth2UserConsents::Cols::_internal_user_id, CompareOperator::EQ, internalUserId
          ) &&
            Criteria(Oauth2UserConsents::Cols::_client_id, CompareOperator::EQ, clientId) &&
            Criteria(Oauth2UserConsents::Cols::_scope_name, CompareOperator::EQ, scope),
          [sharedCb](const std::vector<Oauth2UserConsents> &consents) {
              (*sharedCb)(!consents.empty());
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "hasUserConsent error: " << e.base().what();
              (*sharedCb)(false);
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "hasUserConsent Exception";
        (*sharedCb)(false);
    }
}

void PostgresConsentRepository::saveUserConsent(
  const UserRef &user,
  const std::string &clientId,
  const std::string &scope,
  BoolCallback &&cb
)
{
    int32_t internalUserId = user.internalUserId;

    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));

    try
    {
        Mapper<Oauth2UserConsents> mapper(dbClientMaster_);
        Oauth2UserConsents consent;
        consent.setInternalUserId(internalUserId);
        consent.setClientId(clientId);
        consent.setScopeName(scope);

        mapper.insert(
          consent,
          [sharedCb](const Oauth2UserConsents &insertedConsent) {
              LOG_DEBUG << "Saved user consent: user_id="
                        << insertedConsent.getValueOfInternalUserId()
                        << " client=" << insertedConsent.getValueOfClientId()
                        << " scope=" << insertedConsent.getValueOfScopeName();
              (*sharedCb)(true);
          },
          [sharedCb](const DrogonDbException &e) {
              // Check if it's a constraint violation (consent already exists)
              if (std::string(e.base().what()).find("duplicate key") != std::string::npos)
              {
                  LOG_DEBUG << "User consent already exists (not an error)";
                  (*sharedCb)(true);  // Already exists is considered success
              }
              else
              {
                  LOG_ERROR << "Failed to save user consent: " << e.base().what();
                  (*sharedCb)(false);
              }
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "saveUserConsent Exception";
        (*sharedCb)(false);
    }
}

void PostgresConsentRepository::revokeUserConsent(
  const UserRef &user,
  const std::string &clientId,
  const std::string &scope,
  VoidCallback &&cb
)
{
    int32_t internalUserId = user.internalUserId;

    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));

    try
    {
        Mapper<Oauth2UserConsents> mapper(dbClientMaster_);
        mapper.deleteBy(
          Criteria(
            Oauth2UserConsents::Cols::_internal_user_id, CompareOperator::EQ, internalUserId
          ) &&
            Criteria(Oauth2UserConsents::Cols::_client_id, CompareOperator::EQ, clientId) &&
            Criteria(Oauth2UserConsents::Cols::_scope_name, CompareOperator::EQ, scope),
          [sharedCb](const size_t count) {
              LOG_DEBUG << "Revoked user consent: deleted " << count << " record(s)";
              (*sharedCb)();
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "Failed to revoke user consent: " << e.base().what();
              (*sharedCb)();
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "revokeUserConsent Exception";
        (*sharedCb)();
    }
}

}  // namespace oauth2
