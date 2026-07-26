#include <authforge/drogon/services/DeviceCodeService.h>

#include <authforge/storage/postgres/models/Oauth2DeviceCodes.h>

#include <drogon/drogon.h>

namespace authforge::drogon::services
{

using namespace ::drogon::orm;
using namespace ::drogon_model::oauth2_db;

void DeviceCodeService::createDeviceCode(
  const std::string &deviceCodeHash,
  const std::string &userCode,
  const std::string &clientId,
  const std::string &scope,
  int64_t expiresAt,
  int32_t intervalSeconds,
  ::drogon::orm::DbClientPtr db,
  std::function<void(bool)> &&callback
)
{
    Oauth2DeviceCodes code;
    code.setDeviceCodeHash(deviceCodeHash);
    code.setClientId(clientId);
    code.setScope(scope);
    code.setExpiresAt(expiresAt);
    code.setIntervalSeconds(intervalSeconds);
    code.setUserCode(userCode);
    code.setStatus("pending");

    try
    {
        Mapper<Oauth2DeviceCodes> mapper(db);
        auto sharedCb =
          std::make_shared<std::function<void(bool)>>(std::move(callback));
        mapper.insert(
          code,
          [sharedCb](const Oauth2DeviceCodes &) { (*sharedCb)(true); },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "DeviceCodeService::createDeviceCode failed: "
                        << e.base().what();
              (*sharedCb)(false);
          }
        );
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "DeviceCodeService::createDeviceCode Exception: " << e.what();
        if (callback)
            callback(false);
    }
    catch (...)
    {
        LOG_ERROR << "DeviceCodeService::createDeviceCode Unknown Exception";
        if (callback)
            callback(false);
    }
}

void DeviceCodeService::findByDeviceCodeHash(
  const std::string &deviceCodeHash,
  ::drogon::orm::DbClientPtr db,
  std::function<void(std::shared_ptr<Oauth2DeviceCodes>)> &&callback
)
{
    Criteria crit(
      Oauth2DeviceCodes::Cols::_device_code_hash, CompareOperator::EQ,
      deviceCodeHash
    );
    Mapper<Oauth2DeviceCodes> mapper(db);
    mapper.findOne(
      crit,
      [cb = std::move(callback)](const Oauth2DeviceCodes &code) {
          cb(std::make_shared<Oauth2DeviceCodes>(code));
      },
      [cb = std::move(callback)](const DrogonDbException &) {
          cb(nullptr);
      }
    );
}

void DeviceCodeService::findByUserCode(
  const std::string &userCode,
  ::drogon::orm::DbClientPtr db,
  std::function<void(std::shared_ptr<Oauth2DeviceCodes>)> &&callback
)
{
    Criteria crit(
      Oauth2DeviceCodes::Cols::_user_code, CompareOperator::EQ, userCode
    );
    Mapper<Oauth2DeviceCodes> mapper(db);
    mapper.findOne(
      crit,
      [cb = std::move(callback)](const Oauth2DeviceCodes &code) {
          cb(std::make_shared<Oauth2DeviceCodes>(code));
      },
      [cb = std::move(callback)](const DrogonDbException &) {
          cb(nullptr);
      }
    );
}

void DeviceCodeService::markApproved(
  const std::string &deviceCodeHash,
  const std::string &userId,
  ::drogon::orm::DbClientPtr db,
  std::function<void(bool)> &&callback
)
{
    Criteria crit(
      Oauth2DeviceCodes::Cols::_device_code_hash, CompareOperator::EQ,
      deviceCodeHash
    );
    Mapper<Oauth2DeviceCodes> mapper(db);
    mapper.findOne(
      crit,
      [db, userId, cb = std::move(callback)](const Oauth2DeviceCodes &code) {
          Oauth2DeviceCodes updated = code;
          updated.setUserId(userId);
          updated.setStatus("approved");
          Mapper<Oauth2DeviceCodes>(db).update(
            updated,
            [cb](const size_t) { cb(true); },
            [cb](const DrogonDbException &e) {
                LOG_ERROR << "DeviceCodeService::markApproved update failed: "
                          << e.base().what();
                cb(false);
            }
          );
      },
      [cb = std::move(callback)](const DrogonDbException &e) {
          LOG_ERROR << "DeviceCodeService::markApproved find failed: "
                    << e.base().what();
          cb(false);
      }
    );
}

}  // namespace authforge::drogon::services
