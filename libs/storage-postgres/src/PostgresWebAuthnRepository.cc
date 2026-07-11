#include <authforge/storage/postgres/PostgresWebAuthnRepository.h>

#include <drogon/drogon.h>

namespace authforge::storage::postgres
{

using namespace ::drogon::orm;
using authforge::identity::StoreCredentialOutcome;
using authforge::identity::WebAuthnCredentialLookup;
using authforge::identity::WebAuthnCredentialSummary;

void PostgresWebAuthnRepository::storeCredential(
  int64_t userId,
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
    dbClient_->execSqlAsync(
      "INSERT INTO webauthn_credentials (user_id, credential_id, public_key, name) "
      "VALUES ($1, $2, $3, $4)",
      [sharedCb](const Result &) { (*sharedCb)(StoreCredentialOutcome::Success); },
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
      },
      static_cast<int32_t>(userId),
      credentialId,
      publicKey,
      name
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
    dbClient_->execSqlAsync(
      "SELECT wc.user_id, wc.sign_count, u.public_sub "
      "FROM webauthn_credentials wc "
      "JOIN users u ON wc.user_id = u.id "
      "WHERE wc.credential_id = $1",
      [sharedCb](const Result &r) {
          if (r.empty())
          {
              (*sharedCb)(std::nullopt);
              return;
          }
          WebAuthnCredentialLookup lookup;
          lookup.userId = r[0]["user_id"].as<int64_t>();
          lookup.publicSub = r[0]["public_sub"].as<std::string>();
          lookup.signCount = r[0]["sign_count"].as<int>();
          (*sharedCb)(lookup);
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); },
      credentialId
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
    dbClient_->execSqlAsync(
      "UPDATE webauthn_credentials SET sign_count = $1, last_used_at = NOW() "
      "WHERE credential_id = $2",
      [sharedCb](const Result &) { (*sharedCb)(true); },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); },
      newSignCount,
      credentialId
    );
}

void PostgresWebAuthnRepository::listCredentials(int64_t userId, ListCredentialsCallback &&cb)
{
    if (!dbClient_)
    {
        cb({});
        return;
    }
    auto sharedCb = std::make_shared<ListCredentialsCallback>(std::move(cb));
    dbClient_->execSqlAsync(
      "SELECT credential_id, name, sign_count, created_at, last_used_at "
      "FROM webauthn_credentials WHERE user_id = $1 ORDER BY created_at DESC",
      [sharedCb](const Result &r) {
          std::vector<WebAuthnCredentialSummary> summaries;
          for (const auto &row : r)
          {
              // createdAt/lastUsedAt are intentionally left at their
              // struct defaults (0 / nullopt) -- see
              // IWebAuthnRepository.h's WebAuthnCredentialSummary comment:
              // the production controller's JSON response never surfaces
              // either column today, only credential_id/name/sign_count.
              // Parsing Postgres's raw timestamp text via ::trantor::Date
              // is possible (see the ORM-generated model .cc files' own
              // strptime-based pattern) but not worth the added
              // complexity for fields nothing downstream reads yet.
              WebAuthnCredentialSummary summary;
              summary.credentialId = row["credential_id"].as<std::string>();
              summary.name = row["name"].isNull() ? "" : row["name"].as<std::string>();
              summary.signCount = row["sign_count"].as<int>();
              summaries.push_back(std::move(summary));
          }
          (*sharedCb)(summaries);
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)({}); },
      static_cast<int32_t>(userId)
    );
}

}  // namespace authforge::storage::postgres
