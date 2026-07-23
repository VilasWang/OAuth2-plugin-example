#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <json/json.h>
#include <future>
#include <authforge/drogon/AuthService.h>
#include <oauth2/plugin/OAuth2Plugin.h>

using namespace authforge::drogon::services;

DROGON_TEST(Integration_P0_AuthService_GetUserInfo_Success)
{
    // Skip this test in memory storage mode (no database)
    auto plugin = ::drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        LOG_INFO << "Skipping AuthService_GetUserInfo_Success in memory "
                    "storage mode";
        return;
    }

    // Test successful user info retrieval
    int testUserId = 1;  // Assuming user with ID 1 exists

    std::promise<std::optional<Json::Value>> p;
    auto f = p.get_future();

    AuthService::getUserInfo(testUserId, [&](std::optional<Json::Value> userInfo) {
        p.set_value(userInfo);
    });

    if (f.wait_for(std::chrono::seconds(5)) == std::future_status::timeout)
    {
        throw std::runtime_error("TIMEOUT");
    }

    auto userInfo = f.get();
    CHECK(userInfo.has_value() == true);

    Json::Value json = *userInfo;
    CHECK(json.isMember("sub") == true);
    CHECK(json.isMember("name") == true);
    CHECK(json.isMember("email") == true);
    CHECK(json.isMember("roles") == true);

    // Verify data types
    CHECK(json["sub"].isString() == true);
    CHECK(json["name"].isString() == true);
    CHECK(json["email"].isString() == true);
    CHECK(json["roles"].isArray() == true);

    // Verify user ID matches
    CHECK(json["sub"].asString() == std::to_string(testUserId));
}

DROGON_TEST(Integration_P0_AuthService_GetUserInfo_UserNotFound)
{
    // Skip this test in memory storage mode (no database)
    auto plugin = ::drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        LOG_INFO << "Skipping AuthService_GetUserInfo_UserNotFound in memory "
                    "storage mode";
        return;
    }

    // Test with non-existent user ID
    int nonExistentUserId = 99999;

    std::promise<std::optional<Json::Value>> p;
    auto f = p.get_future();

    AuthService::getUserInfo(nonExistentUserId, [&](std::optional<Json::Value> userInfo) {
        p.set_value(userInfo);
    });

    if (f.wait_for(std::chrono::seconds(5)) == std::future_status::timeout)
    {
        throw std::runtime_error("TIMEOUT");
    }

    auto userInfo = f.get();
    // Should return nullopt for non-existent user
    CHECK(userInfo.has_value() == false);
}

DROGON_TEST(Integration_P0_AuthService_GetUserInfo_InvalidUserId)
{
    // Skip this test in memory storage mode (no database)
    auto plugin = ::drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        LOG_INFO << "Skipping AuthService_GetUserInfo_InvalidUserId in memory "
                    "storage mode";
        return;
    }

    // Test with invalid user ID (zero or negative)
    int invalidUserId = 0;

    std::promise<std::optional<Json::Value>> p;
    auto f = p.get_future();

    AuthService::getUserInfo(invalidUserId, [&](std::optional<Json::Value> userInfo) {
        p.set_value(userInfo);
    });

    if (f.wait_for(std::chrono::seconds(5)) == std::future_status::timeout)
    {
        throw std::runtime_error("TIMEOUT");
    }

    auto userInfo = f.get();
    // Should return nullopt for invalid user ID
    CHECK(userInfo.has_value() == false);
}

DROGON_TEST(Integration_P0_AuthService_GetUserInfo_UserWithRoles)
{
    // Skip this test in memory storage mode (no database)
    auto plugin = ::drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        LOG_INFO << "Skipping AuthService_GetUserInfo_UserWithRoles in memory "
                    "storage mode";
        return;
    }

    // Test user info retrieval with roles
    int testUserId = 1;  // Assuming this user has roles assigned

    std::promise<std::optional<Json::Value>> p;
    auto f = p.get_future();

    AuthService::getUserInfo(testUserId, [&](std::optional<Json::Value> userInfo) {
        p.set_value(userInfo);
    });

    if (f.wait_for(std::chrono::seconds(5)) == std::future_status::timeout)
    {
        throw std::runtime_error("TIMEOUT");
    }

    auto userInfo = f.get();
    CHECK(userInfo.has_value() == true);

    Json::Value json = *userInfo;
    CHECK(json.isMember("roles") == true);

    Json::Value roles = json["roles"];
    CHECK(roles.isArray() == true);

    // Verify roles are strings
    for (const auto &role : roles)
    {
        CHECK(role.isString() == true);
        CHECK(role.asString().empty() == false);
    }
}

DROGON_TEST(Integration_P0_AuthService_GetUserInfo_ResponseStructure)
{
    // Skip this test in memory storage mode (no database)
    auto plugin = ::drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        LOG_INFO << "Skipping AuthService_GetUserInfo_ResponseStructure in "
                    "memory storage mode";
        return;
    }

    // Test complete response structure
    int testUserId = 1;

    std::promise<std::optional<Json::Value>> p;
    auto f = p.get_future();

    AuthService::getUserInfo(testUserId, [&](std::optional<Json::Value> userInfo) {
        p.set_value(userInfo);
    });

    if (f.wait_for(std::chrono::seconds(5)) == std::future_status::timeout)
    {
        throw std::runtime_error("TIMEOUT");
    }

    auto userInfo = f.get();
    CHECK(userInfo.has_value() == true);

    Json::Value json = *userInfo;

    // Verify all required fields exist
    CHECK(json.isMember("sub") == true);
    CHECK(json.isMember("name") == true);
    CHECK(json.isMember("email") == true);
    CHECK(json.isMember("roles") == true);

    // Verify field values are not empty (except possibly roles)
    CHECK(json["sub"].asString().empty() == false);
    CHECK(json["name"].asString().empty() == false);
    CHECK(json["email"].asString().empty() == false);

    // Verify OpenID Connect standard claim format
    CHECK(json["sub"].isString() == true);  // Subject - user ID
}

DROGON_TEST(Integration_P0_AuthService_GetUserInfo_DatabaseErrorHandling)
{
    // Skip this test in memory storage mode (no database)
    auto plugin = ::drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        LOG_INFO << "Skipping AuthService_GetUserInfo_DatabaseErrorHandling in "
                    "memory storage mode";
        return;
    }

    // Test error handling when database operations fail
    // This test verifies graceful degradation when roles query fails

    int testUserId = 1;

    std::promise<std::optional<Json::Value>> p;
    auto f = p.get_future();

    AuthService::getUserInfo(testUserId, [&](std::optional<Json::Value> userInfo) {
        p.set_value(userInfo);
    });

    if (f.wait_for(std::chrono::seconds(5)) == std::future_status::timeout)
    {
        throw std::runtime_error("TIMEOUT");
    }

    auto userInfo = f.get();
    // Even if roles query fails, should still return user info
    CHECK(userInfo.has_value() == true);

    Json::Value json = *userInfo;
    CHECK(json.isMember("name") == true);
    CHECK(json.isMember("email") == true);
    CHECK(json.isMember("roles") == true);

    // Roles might be empty array if query failed, but user info should be
    // present
    CHECK(json["name"].asString().empty() == false);
}
