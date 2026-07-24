#include <authforge/storage/postgres/PostgresMfaRepository.h>

#include <drogon/drogon.h>
#include <json/json.h>

#include <authforge/storage/postgres/models/Users.h>

namespace authforge::storage::postgres
{

using namespace ::drogon::orm;
using authforge::identity::MfaData;
using drogon_model::oauth2_db::Users;

void PostgresMfaRepository::getMfaData(int64_t userId, MfaDataCallback &&cb)
{
    if (!dbClient_)
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<MfaDataCallback>(std::move(cb));
    int32_t userId32 = static_cast<int32_t>(userId);

    Mapper<Users> mapper(dbClient_);
    mapper.findOne(
      Criteria(Users::Cols::_id, CompareOperator::EQ, userId32),
      [sharedCb](const Users &user) {
          MfaData data;
          data.secret = user.getValueOfMfaSecret();
          data.enabled = user.getValueOfMfaEnabled();
          auto backupCodes = user.getMfaBackupCodes();
          if (backupCodes && !backupCodes->empty())
          {
              Json::CharReaderBuilder builder;
              std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
              Json::Value arr;
              std::string errs;
              if (reader->parse(backupCodes->data(),
                                backupCodes->data() + backupCodes->size(), &arr,
                                &errs) &&
                  arr.isArray())
              {
                  for (const auto &code : arr)
                      data.hashedBackupCodes.push_back(code.asString());
              }
          }
          auto pendingCid = user.getMfaPendingClientId();
          data.pendingClientId = pendingCid ? *pendingCid : "";
          auto pendingUri = user.getMfaPendingRedirectUri();
          data.pendingRedirectUri = pendingUri ? *pendingUri : "";
          (*sharedCb)(data);
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(std::nullopt); }
    );
}

void PostgresMfaRepository::setSecret(int64_t userId, const std::string &secret, BoolCallback &&cb)
{
    if (!dbClient_)
    {
        cb(false);
        return;
    }
    int32_t userId32 = static_cast<int32_t>(userId);
    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));

    Mapper<Users> mapper(dbClient_);
    mapper.findOne(
      Criteria(Users::Cols::_id, CompareOperator::EQ, userId32),
      [sharedCb, secret, self = shared_from_this()](const Users &user) {
          Users updated = user;
          updated.setMfaSecret(secret);
          Mapper<Users>(self->dbClient_).update(
            updated,
            [sharedCb](const size_t) { (*sharedCb)(true); },
            [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
          );
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
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
    int32_t userId32 = static_cast<int32_t>(userId);
    Json::Value arr(Json::arrayValue);
    for (const auto &code : hashedBackupCodes)
        arr.append(code);
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    std::string hashedCodesStr = Json::writeString(writer, arr);

    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));

    Mapper<Users> mapper(dbClient_);
    mapper.findOne(
      Criteria(Users::Cols::_id, CompareOperator::EQ, userId32),
      [sharedCb, hashedCodesStr, self = shared_from_this()](const Users &user) {
          Users updated = user;
          updated.setMfaEnabled(true);
          updated.setMfaBackupCodes(hashedCodesStr);
          Mapper<Users>(self->dbClient_).update(
            updated,
            [sharedCb](const size_t) { (*sharedCb)(true); },
            [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
          );
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
    );
}

void PostgresMfaRepository::disable(int64_t userId, BoolCallback &&cb)
{
    if (!dbClient_)
    {
        cb(false);
        return;
    }
    int32_t userId32 = static_cast<int32_t>(userId);
    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));

    Mapper<Users> mapper(dbClient_);
    mapper.findOne(
      Criteria(Users::Cols::_id, CompareOperator::EQ, userId32),
      [sharedCb, self = shared_from_this()](const Users &user) {
          Users updated = user;
          updated.setMfaEnabled(false);
          updated.setMfaSecretToNull();
          updated.setMfaBackupCodesToNull();
          Mapper<Users>(self->dbClient_).update(
            updated,
            [sharedCb](const size_t) { (*sharedCb)(true); },
            [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
          );
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
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
    int32_t userId32 = static_cast<int32_t>(userId);
    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));

    Mapper<Users> mapper(dbClient_);
    mapper.findOne(
      Criteria(Users::Cols::_id, CompareOperator::EQ, userId32),
      [sharedCb, clientId, redirectUri, self = shared_from_this()](const Users &user) {
          Users updated = user;
          updated.setMfaPendingClientId(clientId);
          updated.setMfaPendingRedirectUri(redirectUri);
          Mapper<Users>(self->dbClient_).update(
            updated,
            [sharedCb](const size_t) { (*sharedCb)(true); },
            [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
          );
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
    );
}

void PostgresMfaRepository::clearPendingBinding(int64_t userId, BoolCallback &&cb)
{
    if (!dbClient_)
    {
        cb(false);
        return;
    }
    int32_t userId32 = static_cast<int32_t>(userId);
    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));

    Mapper<Users> mapper(dbClient_);
    mapper.findOne(
      Criteria(Users::Cols::_id, CompareOperator::EQ, userId32),
      [sharedCb, self = shared_from_this()](const Users &user) {
          Users updated = user;
          updated.setMfaPendingClientIdToNull();
          updated.setMfaPendingRedirectUriToNull();
          Mapper<Users>(self->dbClient_).update(
            updated,
            [sharedCb](const size_t) { (*sharedCb)(true); },
            [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
          );
      },
      [sharedCb](const DrogonDbException &) { (*sharedCb)(false); }
    );
}

}  // namespace authforge::storage::postgres
