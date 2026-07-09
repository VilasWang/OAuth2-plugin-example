#include <oauth2/storage/PostgresRoleRepository.h>
#include <drogon/drogon.h>

#include <authforge/storage/postgres/models/Roles.h>
#include <authforge/storage/postgres/models/UserRoles.h>

namespace oauth2
{

using namespace drogon::orm;
using namespace drogon_model::oauth2_db;

void PostgresRoleRepository::getUserRoles(const std::string &userId, StringListCallback &&cb)
{
    if (!dbClientReader_)
    {
        cb({});
        return;
    }

    auto sharedCb = std::make_shared<StringListCallback>(std::move(cb));

    // Check if userId is purely numeric (internal ID) vs UUID (public_sub)
    int uid = 0;
    bool isNumeric = false;
    try
    {
        size_t pos = 0;
        uid = std::stoi(userId, &pos);
        // Only treat as numeric if the ENTIRE string was consumed
        isNumeric = (pos == userId.length());
    }
    catch (...)
    {
        isNumeric = false;
    }

    if (!isNumeric)
    {
        // UUID (public_sub) - resolve to internal ID first
        dbClientReader_->execSqlAsync(
          "SELECT id FROM users WHERE public_sub::text = $1::text",
          [sharedCb, self = shared_from_this(), this](const drogon::orm::Result &r) {
              if (r.empty())
              {
                  (*sharedCb)({});
                  return;
              }
              int32_t resolvedId = r[0]["id"].as<int32_t>();
              getUserRoles(resolvedId, [sharedCb](std::vector<std::string> roles) {
                  (*sharedCb)(roles);
              });
          },
          [sharedCb, userId](const drogon::orm::DrogonDbException &e) {
              LOG_WARN << "getUserRoles: Failed to resolve UUID " << userId << ": "
                       << e.base().what();
              (*sharedCb)({});
          },
          userId
        );
        return;
    }

    // Use ORM instead of raw SQL JOIN
    // Step 1: Find all UserRoles for this user
    try
    {
        Mapper<UserRoles> urMapper(dbClientReader_);
        urMapper.findBy(
          Criteria(UserRoles::Cols::_user_id, CompareOperator::EQ, uid),
          [sharedCb, self = shared_from_this(), this](const std::vector<UserRoles> &userRoles) {
              if (userRoles.empty())
              {
                  (*sharedCb)({});
                  return;
              }

              // Step 2: Extract all role_ids
              std::vector<int32_t> roleIds;
              for (const auto &ur : userRoles)
              {
                  roleIds.push_back(ur.getValueOfRoleId());
              }

              // Step 3: Find all Roles by IDs using Criteria IN
              Mapper<Roles> roleMapper(dbClientReader_);
              roleMapper.findBy(
                Criteria(Roles::Cols::_id, CompareOperator::In, roleIds),
                [sharedCb](const std::vector<Roles> &roles) {
                    std::vector<std::string> roleNames;
                    for (const auto &role : roles)
                    {
                        roleNames.push_back(role.getValueOfName());
                    }
                    (*sharedCb)(roleNames);
                },
                [sharedCb](const DrogonDbException &e) {
                    LOG_ERROR << "getUserRoles: Failed to fetch roles: " << e.base().what();
                    (*sharedCb)({});
                }
              );
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "getUserRoles: Failed to fetch user roles: " << e.base().what();
              (*sharedCb)({});
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "getUserRoles Exception";
        (*sharedCb)({});
    }
}

void PostgresRoleRepository::getUserRoles(int32_t internalUserId, StringListCallback &&cb)
{
    auto sharedCb = std::make_shared<StringListCallback>(std::move(cb));

    try
    {
        Mapper<UserRoles> urMapper(dbClientReader_);
        urMapper.findBy(
          Criteria(UserRoles::Cols::_user_id, CompareOperator::EQ, internalUserId),
          [sharedCb, self = shared_from_this(), this](const std::vector<UserRoles> &userRoles) {
              if (userRoles.empty())
              {
                  (*sharedCb)({});
                  return;
              }

              // Extract all role_ids
              std::vector<int32_t> roleIds;
              for (const auto &ur : userRoles)
              {
                  roleIds.push_back(ur.getValueOfRoleId());
              }

              // Find all Roles by IDs using Criteria IN
              Mapper<Roles> roleMapper(dbClientReader_);
              roleMapper.findBy(
                Criteria(Roles::Cols::_id, CompareOperator::In, roleIds),
                [sharedCb](const std::vector<Roles> &roles) {
                    std::vector<std::string> roleNames;
                    for (const auto &role : roles)
                    {
                        roleNames.push_back(role.getValueOfName());
                    }
                    (*sharedCb)(roleNames);
                },
                [sharedCb](const DrogonDbException &e) {
                    LOG_ERROR << "getUserRoles(int) Failed to fetch roles: " << e.base().what();
                    (*sharedCb)({});
                }
              );
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "getUserRoles(int) Failed to fetch user roles: " << e.base().what();
              (*sharedCb)({});
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "getUserRoles(int) Exception";
        (*sharedCb)({});
    }
}

}  // namespace oauth2
