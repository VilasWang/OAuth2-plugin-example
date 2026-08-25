#pragma once

// Task B5 (fulla-sdk-refactor): Device code CRUD operations extracted
// from DeviceAuthController and OAuth2StandardController. Replaces raw SQL
// on oauth2_device_codes with Mapper<T> + Criteria (per db-operations.md).
//
// Lives in libs/drogon (Adapter layer, namespace
// fulla::drogon::services).

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace drogon
{
namespace orm
{
class DbClient;
using DbClientPtr = std::shared_ptr<DbClient>;
}  // namespace orm
}  // namespace drogon

namespace drogon_model::fulla_db
{
class Oauth2DeviceCodes;
}

namespace fulla::drogon::services
{

class DeviceCodeService
{
  public:
    /// Create a new device authorization code entry.
    /// Replaces: INSERT INTO oauth2_device_codes (...) VALUES (...)
    static void createDeviceCode(
      const std::string &deviceCodeHash,
      const std::string &userCode,
      const std::string &clientId,
      const std::string &scope,
      int64_t expiresAt,
      int32_t intervalSeconds,
      ::drogon::orm::DbClientPtr db,
      std::function<void(bool)> &&callback
    );

    /// Find a device code by its hash. Returns nullptr if not found.
    /// Replaces: SELECT ... FROM oauth2_device_codes WHERE device_code_hash = $1
    static void findByDeviceCodeHash(
      const std::string &deviceCodeHash,
      ::drogon::orm::DbClientPtr db,
      std::function<void(std::shared_ptr<::drogon_model::fulla_db::Oauth2DeviceCodes>)> &&callback
    );

    /// Mark a device code as consumed (status = 'approved', set user_id).
    /// Replaces: UPDATE oauth2_device_codes SET status = 'approved', user_id = $1
    static void markApproved(
      const std::string &deviceCodeHash,
      const std::string &userId,
      ::drogon::orm::DbClientPtr db,
      std::function<void(bool)> &&callback
    );

    /// Find a device code by its user_code. Returns nullptr if not found.
    static void findByUserCode(
      const std::string &userCode,
      ::drogon::orm::DbClientPtr db,
      std::function<void(std::shared_ptr<::drogon_model::fulla_db::Oauth2DeviceCodes>)> &&callback
    );
};

}  // namespace fulla::drogon::services
