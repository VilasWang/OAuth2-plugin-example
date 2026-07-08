#include <oauth2/storage/PostgresUserRepository.h>
#include <drogon/drogon.h>

namespace oauth2
{

using namespace drogon::orm;

void PostgresUserRepository::getUserInfo(const std::string &userId, OptionalJsonCallback &&cb)
{
    // Check if userId is purely numeric (internal ID) vs UUID (public_sub)
    bool isNumeric = false;
    int32_t numericUserId = 0;
    try
    {
        size_t pos = 0;
        numericUserId = std::stoi(userId, &pos);
        isNumeric = (pos == userId.length());
    }
    catch (...)
    {
        isNumeric = false;
    }

    if (isNumeric)
    {
        getUserInfo(numericUserId, std::move(cb));
        return;
    }

    // UUID (public_sub) lookup
    if (!dbClientReader_)
    {
        cb(std::nullopt);
        return;
    }

    auto sharedCb = std::make_shared<OptionalJsonCallback>(std::move(cb));
    dbClientReader_->execSqlAsync(
      "SELECT id, username, email FROM users WHERE public_sub::text = $1::text",
      [sharedCb](const Result &result) {
          if (result.empty())
          {
              (*sharedCb)(std::nullopt);
              return;
          }
          auto row = result[0];
          Json::Value userInfo;
          userInfo["id"] = row["id"].as<int32_t>();
          if (!row["username"].isNull())
              userInfo["username"] = row["username"].as<std::string>();
          if (!row["email"].isNull())
              userInfo["email"] = row["email"].as<std::string>();
          (*sharedCb)(userInfo);
      },
      [sharedCb](const DrogonDbException &e) {
          LOG_WARN << "getUserInfo by public_sub failed: " << e.base().what();
          (*sharedCb)(std::nullopt);
      },
      userId
    );
}

void PostgresUserRepository::getUserInfo(int32_t internalUserId, OptionalJsonCallback &&cb)
{
    // Query user info from database
    std::string query = "SELECT username, email FROM users WHERE id = $1";

    dbClientReader_->execSqlAsync(
      query,
      [internalUserId, cb = std::move(cb)](const Result &result) mutable {
          try
          {
              if (result.size() == 0)
              {
                  cb(std::nullopt);
                  return;
              }

              auto row = result[0];
              Json::Value userInfo;
              userInfo["id"] = internalUserId;

              // Get username (first column)
              if (!row["username"].isNull())
              {
                  userInfo["username"] = row["username"].as<std::string>();
              }

              // Get email (second column, optional)
              if (!row["email"].isNull())
              {
                  userInfo["email"] = row["email"].as<std::string>();
              }

              cb(userInfo);
          }
          catch (const std::exception &e)
          {
              LOG_ERROR << "Failed to parse user info for user: " << internalUserId
                        << ", error: " << e.what();
              cb(std::nullopt);
          }
      },
      [cb](const DrogonDbException &e) mutable {
          LOG_ERROR << "Database error getting user info: " << e.base().what();
          cb(std::nullopt);
      },
      internalUserId
    );
}

}  // namespace oauth2
