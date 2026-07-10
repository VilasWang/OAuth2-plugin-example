#include <authforge/storage/postgres/PostgresMfaRepository.h>

#include <drogon/drogon.h>
#include <json/json.h>

namespace authforge::storage::postgres
{

using namespace ::drogon::orm;
using authforge::identity::MfaData;

void PostgresMfaRepository::getMfaData(int64_t userId, MfaDataCallback &&cb)
{
    if (!dbClient_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<MfaDataCallback>(std::move(cb));
    dbClient_->execSqlAsync(
      "SELECT mfa_secret, mfa_enabled, mfa_backup_codes, mfa_pending_client_id, "
      "mfa_pending_redirect_uri FROM users WHERE id = $1",
      [sharedCb](const Result &r) {
          if (r.empty())
          {
              (*sharedCb)(std::nullopt);
              return;
          }
          MfaData data;
          data.secret = r[0]["mfa_secret"].isNull() ? "" : r[0]["mfa_secret"].as<std::string>();
          data.enabled = !r[0]["mfa_enabled"].isNull() && r[0]["mfa_enabled"].as<bool>();
          if (!r[0]["mfa_backup_codes"].isNull())
          {
              std::string raw = r[0]["mfa_backup_codes"].as<std::string>();
              Json::CharReaderBuilder builder;
              std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
              Json::Value arr;
              std::string errs;
              if (reader->parse(raw.data(), raw.data() + raw.size(), &arr, &errs) && arr.isArray())
              {
                  for (const auto &code : arr)
                      data.hashedBackupCodes.push_back(code.asString());
              }
          }
          data.pendingClientId = r[0]["mfa_pending_client_id"].isNull()
                                    ? ""
                                    : r[0]["mfa_pending_client_id"].as<std::string>();
          data.pendingRedirectUri = r[0]["mfa_pending_redirect_uri"].isNull()
                                       ? ""
                                       : r[0]["mfa_pending_redirect_uri"].as<std::string>();
          (*sharedCb)(data);
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); },
      static_cast<int32_t>(userId)
    );
}

void PostgresMfaRepository::setSecret(
  int64_t userId,
  const std::string &secret,
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
      "UPDATE users SET mfa_secret = $1 WHERE id = $2",
      [sharedCb](const Result &) { (*sharedCb)(true); },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); },
      secret,
      static_cast<int32_t>(userId)
    );
}

void PostgresMfaRepository::enable(
  int64_t userId,
  const std::vector<std::string> &hashedBackupCodes,
  BoolCallback &&cb
)
{
    if (!dbClient_)
    {
        cb(false);
        return;
    }
    Json::Value arr(Json::arrayValue);
    for (const auto &code : hashedBackupCodes)
        arr.append(code);
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    std::string hashedCodesStr = Json::writeString(writer, arr);

    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));
    dbClient_->execSqlAsync(
      "UPDATE users SET mfa_enabled = true, mfa_backup_codes = $1 WHERE id = $2",
      [sharedCb](const Result &) { (*sharedCb)(true); },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); },
      hashedCodesStr,
      static_cast<int32_t>(userId)
    );
}

void PostgresMfaRepository::disable(int64_t userId, BoolCallback &&cb)
{
    if (!dbClient_)
    {
        cb(false);
        return;
    }
    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));
    dbClient_->execSqlAsync(
      "UPDATE users SET mfa_enabled = false, mfa_secret = NULL, mfa_backup_codes = NULL "
      "WHERE id = $1",
      [sharedCb](const Result &) { (*sharedCb)(true); },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); },
      static_cast<int32_t>(userId)
    );
}

void PostgresMfaRepository::setPendingBinding(
  int64_t userId,
  const std::string &clientId,
  const std::string &redirectUri,
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
      "UPDATE users SET mfa_pending_client_id = $1, mfa_pending_redirect_uri = $2 WHERE id = $3",
      [sharedCb](const Result &) { (*sharedCb)(true); },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); },
      clientId,
      redirectUri,
      static_cast<int32_t>(userId)
    );
}

void PostgresMfaRepository::clearPendingBinding(int64_t userId, BoolCallback &&cb)
{
    if (!dbClient_)
    {
        cb(false);
        return;
    }
    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));
    dbClient_->execSqlAsync(
      "UPDATE users SET mfa_pending_client_id = NULL, mfa_pending_redirect_uri = NULL "
      "WHERE id = $1",
      [sharedCb](const Result &) { (*sharedCb)(true); },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); },
      static_cast<int32_t>(userId)
    );
}

}  // namespace authforge::storage::postgres
