#include <authforge/storage/redis/RedisClientRepository.h>
#include <authforge/common/utils/ConstantTimeCompare.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <json/json.h>
#include <sstream>
#include <algorithm>

// NOTE: the original RedisOAuth2Storage emitted an OperationTimer metric here
// via <authforge/drogon/observability/Metrics.h> (owned by OAuth2Plugin). That
// header is not yet relocated to libs/drogon (scheduled for Phase 3 of the
// authforge-sdk-refactor, see .kiro/specs/authforge-sdk-refactor/
// DIRECTORY_RESTRUCTURE_PLAN.md §3b). storage-postgres/storage-memory -- the
// sibling packages this mirrors -- shed the same coupling during their
// migrations and carry no OAuth2Plugin include. To keep this package
// self-contained against its declared deps (Drogon + jsoncpp +
// authforge::oauth2) and avoid a storage-redis -> OAuth2Plugin dependency
// cycle (this package is added BEFORE OAuth2Plugin in the root CMakeLists),
// the OperationTimer instrumentation is dropped here. It will return when
// observability relocates to libs/drogon/observability/ in Phase 3 and
// becomes a proper (cycle-free) dependency of this package.

namespace authforge::storage::redis
{

// Task 27.5: callback + DTO aliases for the new base interface; safe at namespace scope here (this
// .cc does not include IOAuth2Storage.h, so no oauth2::* clash).
using OAuth2Client = ::authforge::oauth2::model::OAuth2Client;
using ClientCallback = IClientRepositoryBase::ClientCallback;
using BoolCallback = IClientRepositoryBase::BoolCallback;

using namespace ::drogon;
using namespace ::drogon::nosql;

namespace
{
// Verbatim copy of the anonymous-namespace JSON helper from
// RedisOAuth2Storage.cc -- used by getClient below (validateClient in the
// original didn't need it).
Json::Value parseJson(const std::string &jsonStr)
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream s(jsonStr);
    if (!Json::parseFromStream(builder, s, &root, &errs))
    {
        LOG_ERROR << "Redis JSON parse error: " << errs;
        return Json::nullValue;
    }
    return root;
}
}  // namespace

void RedisClientRepository::getClient(const std::string &clientId, ClientCallback &&cb)
{
    if (!redisClient_)
    {
        LOG_ERROR << "Redis client is not initialized!";
        cb(std::nullopt);
        return;
    }
    std::string cmd = "HGETALL oauth2:client:" + clientId;
    redisClient_->execCommandAsync(
      [cb, clientId](const RedisResult &result) {
          if (result.type() == RedisResultType::kNil || result.type() != RedisResultType::kArray)
          {
              cb(std::nullopt);
              return;
          }
          auto arr = result.asArray();
          if (arr.empty())
          {
              cb(std::nullopt);
              return;
          }

          OAuth2Client client;
          client.clientId = clientId;
          for (size_t i = 0; i < arr.size(); i += 2)
          {
              if (i + 1 >= arr.size())
                  break;
              std::string key = arr[i].asString();
              std::string val = arr[i + 1].asString();
              if (key == "secret")
                  client.clientSecretHash = val;
              else if (key == "salt")
                  client.salt = val;
              else if (key == "redirect_uris")
              {
                  auto json = parseJson(val);
                  if (json.isArray())
                  {
                      for (const auto &uri : json)
                          client.redirectUris.push_back(uri.asString());
                  }
              }
          }
          cb(client);
      },
      [cb](const RedisException &e) {
          LOG_ERROR << "Redis getClient error: " << e.what();
          cb(std::nullopt);
      },
      cmd.c_str()
    );
}

void RedisClientRepository::validateClient(
  const std::string &clientId,
  const std::string &clientSecret,
  BoolCallback &&cb
)
{
    if (!redisClient_)
    {
        LOG_ERROR << "Redis client is not initialized!";
        cb(false);
        return;
    }
    LOG_DEBUG << "validateClient called for: " << clientId;
    if (clientSecret.empty())
    {
        std::string cmd = "EXISTS oauth2:client:" + clientId;
        redisClient_->execCommandAsync(
          [cb](const RedisResult &result) { cb(result.asInteger() == 1); },
          [cb](const RedisException &e) {
              LOG_ERROR << "Redis EXISTS error: " << e.what();
              cb(false);
          },
          cmd.c_str()
        );
    }
    else
    {
        std::string cmd = "HMGET oauth2:client:" + clientId + " secret salt";
        redisClient_->execCommandAsync(
          [cb, inputSecret = clientSecret](const RedisResult &result) {
              LOG_DEBUG << "validateClient HMGET result received";
              if (
                result.type() == RedisResultType::kNil || result.type() != RedisResultType::kArray
              )
              {
                  cb(false);
                  return;
              }
              auto arr = result.asArray();
              if (
                arr.size() < 2 || arr[0].type() == RedisResultType::kNil ||
                arr[1].type() == RedisResultType::kNil
              )
              {
                  // HMGET returns an array of nil elements (not a top-level
                  // nil) when the hash key doesn't exist or the requested
                  // fields are missing. RedisResult::asString() throws for
                  // a kNil element, so this must be checked before calling
                  // it (bug fix: previously crashed the process here for a
                  // nonexistent client with a non-empty secret).
                  cb(false);
                  return;
              }

              std::string storedHash = arr[0].asString();
              std::string salt = arr[1].asString();
              std::string input = inputSecret + salt;
              std::string calculatedHash = ::drogon::utils::getSha256(input.data(), input.length());

              // Case-insensitive comparison
              std::transform(
                calculatedHash.begin(), calculatedHash.end(), calculatedHash.begin(), ::tolower
              );
              std::transform(storedHash.begin(), storedHash.end(), storedHash.begin(), ::tolower);

              // F-004 (OAuth 2.0 Security BCP §4.9): constant-time compare.
              // The previous std::string::operator== short-circuits on the
              // first differing byte (timing side channel). The debug log
              // that printed the match result was removed for the same
              // reason (comparison-outcome leakage).
              size_t cmpLen = (calculatedHash.length() < storedHash.length())
                                ? calculatedHash.length()
                                : storedHash.length();
              bool match =
                (::authforge::common::utils::constantTimeMemcmp(
                   calculatedHash.c_str(), storedHash.c_str(), cmpLen
                 ) == 0) &&
                calculatedHash.length() == storedHash.length();
              cb(match);
          },
          [cb](const RedisException &e) {
              LOG_ERROR << "Redis validateClient HMGET error: " << e.what();
              cb(false);
          },
          cmd.c_str()
        );
    }
}

}  // namespace authforge::storage::redis
