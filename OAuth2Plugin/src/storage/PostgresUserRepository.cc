#include <oauth2/storage/PostgresUserRepository.h>
#include <drogon/drogon.h>

#include <authforge/storage/postgres/models/Users.h>

namespace oauth2
{

using namespace drogon::orm;
using drogon_model::oauth2_db::Users;

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
    LOG_DEBUG << "[PG-UserRepo] getUserInfo by public_sub: " << userId;

    Mapper<Users> mapper(dbClientReader_);
    mapper.findOne(
      Criteria(Users::Cols::_public_sub, CompareOperator::EQ, userId),
      [sharedCb](const Users &user) {
          Json::Value userInfo;
          userInfo["id"] = static_cast<int32_t>(user.getValueOfId());
          userInfo["username"] = user.getValueOfUsername();
          userInfo["email"] = user.getValueOfEmail();
          (*sharedCb)(userInfo);
      },
      [sharedCb](const DrogonDbException &e) {
          LOG_WARN << "getUserInfo by public_sub failed: " << e.base().what();
          (*sharedCb)(std::nullopt);
      }
    );
}

void PostgresUserRepository::getUserInfo(int32_t internalUserId, OptionalJsonCallback &&cb)
{
    if (!dbClientReader_)
    {
        cb(std::nullopt);
        return;
    }

    auto sharedCb = std::make_shared<OptionalJsonCallback>(std::move(cb));

    Mapper<Users> mapper(dbClientReader_);
    mapper.findOne(
      Criteria(Users::Cols::_id, CompareOperator::EQ, internalUserId),
      [sharedCb, internalUserId](const Users &user) {
          try
          {
              Json::Value userInfo;
              userInfo["id"] = internalUserId;
              userInfo["username"] = user.getValueOfUsername();
              userInfo["email"] = user.getValueOfEmail();
              (*sharedCb)(userInfo);
          }
          catch (const std::exception &e)
          {
              LOG_ERROR << "Failed to parse user info for user: " << internalUserId
                        << ", error: " << e.what();
              (*sharedCb)(std::nullopt);
          }
      },
      [sharedCb](const DrogonDbException &e) {
          LOG_ERROR << "Database error getting user info: " << e.base().what();
          (*sharedCb)(std::nullopt);
      }
    );
}

}  // namespace oauth2
