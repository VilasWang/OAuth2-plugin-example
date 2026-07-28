#include <authforge/storage/postgres/PostgresWebAuthnRepository.h>

#include <drogon/drogon.h>

#include <authforge/storage/postgres/models/Users.h>
#include <authforge/storage/postgres/models/WebauthnCredentials.h>

namespace authforge::storage::postgres
{

using namespace ::drogon::orm;
using authforge::identity::StoreCredentialOutcome;
using authforge::identity::WebAuthnCredentialLookup;
using authforge::identity::WebAuthnCredentialSummary;
using drogon_model::oauth2_db::Users;
using drogon_model::oauth2_db::WebauthnCredentials;

void PostgresWebAuthnRepository::storeCredential(
  int32_t userId,
  const std::string &credentialId,
  const std::string &publicKey,
  const std::string &name,
  StoreCredentialCallback &&cb
)
{
    if (!dbClient_)
    {
        cb(StoreCredentialOutcome::Error);
        return;
    }
    auto sharedCb = std::make_shared<StoreCredentialCallback>(std::move(cb));

    WebauthnCredentials cred;
    cred.setUserId(userId);
    cred.setCredentialId(credentialId);
    cred.setPublicKey(publicKey);
    cred.setName(name);

    Mapper<WebauthnCredentials> mapper(dbClient_);
    mapper.insert(
      cred,
      [sharedCb](const WebauthnCredentials &) { (*sharedCb)(StoreCredentialOutcome::Success); },
      [sharedCb](const DrogonDbException &e) {
          const std::string what = e.base().what();
          if (
            what.find("webauthn_credentials") != std::string::npos &&
            what.find("credential_id") != std::string::npos
          )
          {
              (*sharedCb)(StoreCredentialOutcome::DuplicateCredentialId);
              return;
          }
          (*sharedCb)(StoreCredentialOutcome::Error);
      }
    );
}

void PostgresWebAuthnRepository::findByCredentialId(
  const std::string &credentialId,
  CredentialLookupCallback &&cb
)
{
    if (!dbClient_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<CredentialLookupCallback>(std::move(cb));

    // 查询 webauthn_credentials 获取 user_id 和 sign_count
    Mapper<WebauthnCredentials> wcMapper(dbClient_);
    wcMapper.findOne(
      Criteria(WebauthnCredentials::Cols::_credential_id, CompareOperator::EQ, credentialId),
      [sharedCb, self = shared_from_this()](const WebauthnCredentials &wc) {
          int32_t userId32 = wc.getValueOfUserId();

          // 查询 users 获取 public_sub
          Mapper<Users> userMapper(self->dbClient_);
          userMapper.findOne(
            Criteria(Users::Cols::_id, CompareOperator::EQ, userId32),
            [sharedCb, wc](const Users &user) {
                WebAuthnCredentialLookup lookup;
                lookup.userId = wc.getValueOfUserId();
                lookup.publicSub = user.getValueOfPublicSub();
                lookup.signCount = static_cast<int>(wc.getValueOfSignCount());
                (*sharedCb)(lookup);
            },
            [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
          );
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
    );
}

void PostgresWebAuthnRepository::updateSignCount(
  const std::string &credentialId,
  int newSignCount,
  BoolCallback &&cb
)
{
    if (!dbClient_)
    {
        cb(false);
        return;
    }
    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));

    Mapper<WebauthnCredentials> mapper(dbClient_);
    mapper.findOne(
      Criteria(WebauthnCredentials::Cols::_credential_id, CompareOperator::EQ, credentialId),
      [sharedCb, newSignCount, self = shared_from_this()](const WebauthnCredentials &found) {
          WebauthnCredentials updated;
          updated.setCredentialId(found.getValueOfCredentialId());
          updated.setSignCount(static_cast<int32_t>(newSignCount));
          updated.setLastUsedAt(::trantor::Date::now());

          Mapper<WebauthnCredentials>(self->dbClient_)
            .update(
              updated,
              [sharedCb](const size_t) { (*sharedCb)(true); },
              [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
            );
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
    );
}

void PostgresWebAuthnRepository::listCredentials(int32_t userId, ListCredentialsCallback &&cb)
{
    if (!dbClient_)
    {
        cb({});
        return;
    }
    auto sharedCb = std::make_shared<ListCredentialsCallback>(std::move(cb));

    Mapper<WebauthnCredentials> mapper(dbClient_);
    mapper.findBy(
      Criteria(WebauthnCredentials::Cols::_user_id, CompareOperator::EQ, userId),
      [sharedCb](const std::vector<WebauthnCredentials> &creds) {
          std::vector<WebAuthnCredentialSummary> summaries;
          for (const auto &wc : creds)
          {
              WebAuthnCredentialSummary summary;
              summary.credentialId = wc.getValueOfCredentialId();
              auto namePtr = wc.getName();
              summary.name = namePtr ? *namePtr : "";
              summary.signCount = static_cast<int>(wc.getValueOfSignCount());
              summaries.push_back(std::move(summary));
          }
          (*sharedCb)(summaries);
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)({}); }
    );
}

}  // namespace authforge::storage::postgres
