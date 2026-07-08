#include <oauth2/storage/RedisSubjectMappingRepository.h>

namespace oauth2
{

using namespace drogon;
using namespace drogon::nosql;

void RedisSubjectMappingRepository::getInternalUserId(
  const std::string &subject,
  const std::string &provider,
  OptionalIntCallback &&cb
)
{
    // Redis implementation using hash maps
    // Key: oauth2:subject_mapping:{provider}:{subject}
    if (!redisClient_)
    {
        cb(std::nullopt);
        return;
    }

    std::string key = "oauth2:subject_mapping:" + provider + ":" + subject;
    redisClient_->execCommandAsync(
      [cb](const RedisResult &result) {
          if (result.type() == RedisResultType::kNil)
          {
              cb(std::nullopt);
              return;
          }
          // Redis HGET returns string value or nil
          std::string userIdStr = result.asString();
          try
          {
              int32_t userId = std::stoi(userIdStr);
              cb(userId);
          }
          catch (...)
          {
              LOG_ERROR << "Failed to parse user ID from Redis: " << userIdStr;
              cb(std::nullopt);
          }
      },
      [cb](const RedisException &e) {
          LOG_ERROR << "Redis getInternalUserId error: " << e.what();
          cb(std::nullopt);
      },
      "HGET %s user_id",
      key.c_str()
    );
}

void RedisSubjectMappingRepository::createSubjectMapping(
  const std::string &subject,
  int32_t internalUserId,
  const std::string &provider,
  BoolCallback &&cb
)
{
    if (!redisClient_)
    {
        cb(false);
        return;
    }

    std::string key = "oauth2:subject_mapping:" + provider + ":" + subject;
    std::string userIdStr = std::to_string(internalUserId);

    redisClient_->execCommandAsync(
      [cb](const RedisResult &result) {
          // HSET returns 1 for new field, 0 for updated field
          cb(true);
      },
      [cb, subject, provider](const RedisException &e) {
          LOG_ERROR << "Failed to create subject mapping in Redis: " << e.what();
          cb(false);
      },
      "HSET %s user_id %s",
      key.c_str(),
      userIdStr.c_str()
    );
}

}  // namespace oauth2
