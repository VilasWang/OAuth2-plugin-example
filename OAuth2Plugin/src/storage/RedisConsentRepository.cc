#include <oauth2/storage/RedisConsentRepository.h>
#include <chrono>

namespace oauth2
{

using namespace drogon;
using namespace drogon::nosql;

void RedisConsentRepository::hasUserConsent(
  const UserRef &user,
  const std::string &clientId,
  const std::string &scope,
  BoolCallback &&cb
)
{
    // F4: unwrap the opaque UserRef to the internal key the Redis key needs.
    // See UserRef.h -- this is the one place (storage-layer implementation)
    // permitted to do so.
    int32_t internalUserId = user.internalUserId;

    if (!redisClient_)
    {
        cb(false);
        return;
    }

    std::string key =
      "oauth2:consent:" + std::to_string(internalUserId) + ":" + clientId + ":" + scope;
    redisClient_->execCommandAsync(
      [cb](const RedisResult &result) {
          // EXISTS returns 1 if key exists, 0 otherwise
          cb(result.type() != RedisResultType::kNil);
      },
      [cb](const RedisException &e) {
          LOG_ERROR << "Redis hasUserConsent error: " << e.what();
          cb(false);
      },
      "EXISTS %s",
      key.c_str()
    );
}

void RedisConsentRepository::saveUserConsent(
  const UserRef &user,
  const std::string &clientId,
  const std::string &scope,
  BoolCallback &&cb
)
{
    int32_t internalUserId = user.internalUserId;

    if (!redisClient_)
    {
        cb(false);
        return;
    }

    std::string key =
      "oauth2:consent:" + std::to_string(internalUserId) + ":" + clientId + ":" + scope;
    auto now = std::chrono::system_clock::now();
    size_t nowSec =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    size_t ttl = 30 * 24 * 3600;  // 30 days

    redisClient_->execCommandAsync(
      [cb](const RedisResult &) { cb(true); },
      [cb](const RedisException &e) {
          LOG_ERROR << "Failed to save user consent: " << e.what();
          cb(false);
      },
      "SETEX %s %d %d",
      key.c_str(),
      ttl,
      nowSec
    );
}

void RedisConsentRepository::revokeUserConsent(
  const UserRef &user,
  const std::string &clientId,
  const std::string &scope,
  VoidCallback &&cb
)
{
    int32_t internalUserId = user.internalUserId;

    if (!redisClient_)
    {
        if (cb)
            cb();
        return;
    }

    std::string key =
      "oauth2:consent:" + std::to_string(internalUserId) + ":" + clientId + ":" + scope;
    redisClient_->execCommandAsync(
      [cb](const RedisResult &) {
          if (cb)
              cb();
      },
      [cb](const RedisException &) {
          if (cb)
              cb();
      },
      "DEL %s",
      key.c_str()
    );
}

}  // namespace oauth2
