#include <fulla/storage/postgres/PostgresWebAuthnRepository.h>

#include <drogon/drogon.h>

#include <fulla/storage/postgres/models/Users.h>
#include <fulla/storage/postgres/models/WebauthnCredentials.h>

namespace fulla::storage::postgres
{

using namespace ::drogon::orm;
using fulla::identity::StoreCredentialOutcome;
using fulla::identity::WebAuthnCredentialLookup;
using fulla::identity::WebAuthnCredentialSummary;
using drogon_model::fulla_db::Users;
using drogon_model::fulla_db::WebauthnCredentials;

namespace
{
int64_t nowEpochSeconds()
{
    return static_cast<int64_t>(::trantor::Date::now().secondsSinceEpoch());
}
}  // namespace

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

    // Guard: Mapper construction inside try (db-operations.md rule 1).
    try
    {
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
    catch (...)
    {
        (*sharedCb)(StoreCredentialOutcome::Error);
    }
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
    try
    {
        Mapper<WebauthnCredentials> wcMapper(dbClient_);
        wcMapper.findOne(
          Criteria(WebauthnCredentials::Cols::_credential_id, CompareOperator::EQ, credentialId),
          [sharedCb, self = shared_from_this()](const WebauthnCredentials &wc) {
              int32_t userId32 = wc.getValueOfUserId();

              // 查询 users 获取 public_sub。Guard: 每个 Mapper 构造独立
              // try-catch —— 外层保护不到异步回调内部
              // (db-operations.md rule 3)。
              try
              {
                  Mapper<Users> userMapper(self->dbClient_);
                  // Liveness gate (#142 review M-1): a soft-deleted or
                  // locked user must not authenticate via passkey — parity
                  // with the password and social paths (V024/#54 contract:
                  // "disabled" means cannot log in through ANY flow).
                  userMapper.findOne(
                    Criteria(Users::Cols::_id, CompareOperator::EQ, userId32) &&
                      Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull),
                    [sharedCb, wc](const Users &user) {
                        if (user.getValueOfLockedUntil() > nowEpochSeconds())
                        {
                            // Generic nullopt — same failure shape as an
                            // unknown credential, no account-status leak.
                            (*sharedCb)(std::nullopt);
                            return;
                        }
                        WebAuthnCredentialLookup lookup;
                        lookup.userId = wc.getValueOfUserId();
                        lookup.publicSub = user.getValueOfPublicSub();
                        lookup.signCount = static_cast<int>(wc.getValueOfSignCount());
                        // #142: assertion signatures verify against the
                        // STORED COSE key.
                        lookup.publicKey = wc.getValueOfPublicKey();
                        (*sharedCb)(lookup);
                    },
                    [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
                  );
              }
              catch (...)
              {
                  (*sharedCb)(std::nullopt);
              }
          },
          [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
        );
    }
    catch (...)
    {
        (*sharedCb)(std::nullopt);
    }
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

    try
    {
        Mapper<WebauthnCredentials> mapper(dbClient_);
        mapper.findOne(
          Criteria(WebauthnCredentials::Cols::_credential_id, CompareOperator::EQ, credentialId),
          [sharedCb, newSignCount, self = shared_from_this()](const WebauthnCredentials &found) {
          // Update from the FOUND row: a freshly-constructed object would
          // carry empty NOT NULL columns (user_id/public_key), failing the
          // whole UPDATE silently under the best-effort callback (#142
          // found sign_count never advancing).
          WebauthnCredentials updated(found);
          updated.setSignCount(static_cast<int32_t>(newSignCount));
          updated.setLastUsedAt(::trantor::Date::now());

          try
          {
              Mapper<WebauthnCredentials>(self->dbClient_)
                .update(
                  updated,
                  [sharedCb](const size_t) { (*sharedCb)(true); },
                  [sharedCb](const DrogonDbException &e) {
                      LOG_WARN << "updateSignCount failed: " << e.base().what();
                      (*sharedCb)(false);
                  }
                );
          }
          catch (...)
          {
              (*sharedCb)(false);
          }
      },
          [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
        );
    }
    catch (...)
    {
        (*sharedCb)(false);
    }
}

void PostgresWebAuthnRepository::listCredentials(int32_t userId, ListCredentialsCallback &&cb)
{
    if (!dbClient_)
    {
        cb({});
        return;
    }
    auto sharedCb = std::make_shared<ListCredentialsCallback>(std::move(cb));

    try
    {
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
    catch (...)
    {
        (*sharedCb)({});
    }
}

}  // namespace fulla::storage::postgres
