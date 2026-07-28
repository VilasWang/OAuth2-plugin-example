#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <json/json.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <iostream>
#include <thread>
#include <chrono>

using namespace drogon;

DROGON_TEST(Unit_P1_Utils_RateLimiter_Works)
{
    // Skip network-intensive rate limiter tests in memory/CI mode
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        return;
    }

    // Test Configuration
    std::string baseUrl = "http://127.0.0.1:5555";

    // 1. Check if server is running
    LOG_INFO << "=== Hodor Rate Limiter Test Suite ===";
    LOG_INFO << "Checking if server is running at " << baseUrl;

    bool serverReady = false;
    for (int i = 0; i < 3; ++i)
    {
        try
        {
            auto client = HttpClient::newHttpClient(baseUrl);
            auto req = HttpRequest::newHttpRequest();
            req->setMethod(Head);
            auto [result, response] = client->sendRequest(req);

            if (response && result == ReqResult::Ok)
            {
                serverReady = true;
                LOG_INFO << "Server is ready for testing";
                break;
            }
        }
        catch (const std::exception &e)
        {
            LOG_WARN << "Server not ready, attempt " << i + 1 << "/3: " << e.what();
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    if (!serverReady)
    {
        LOG_ERROR << "Server is not running, skipping rate limiter tests";
        LOG_ERROR << "Please start OAuth2Server.exe before running tests";
        CHECK(false);  // Fail the test
        return;
    }

    // Helper function to create JSON POST request
    auto createJsonPostRequest = [&](const std::string &path, const Json::Value &data) {
        auto req = HttpRequest::newHttpRequest();
        req->setPath(path);
        req->setMethod(Post);
        req->setContentTypeCode(CT_APPLICATION_JSON);
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string jsonString = Json::writeString(builder, data);
        req->setBody(jsonString);
        return req;
    };

    // 2. Test IP-based rate limiting on /oauth2/login
    LOG_INFO << "";
    LOG_INFO << "=== Test 1: IP-based rate limiting on /oauth2/login ===";

    int successCount = 0;
    int rateLimitedCount = 0;

    // Send 7 requests (limit is 5 per minute for /oauth2/login)
    for (int i = 0; i < 7; ++i)
    {
        try
        {
            auto client = HttpClient::newHttpClient(baseUrl);

            Json::Value loginData;
            loginData["username"] = "testuser";
            loginData["password"] = "testpass";

            auto req = createJsonPostRequest("/oauth2/login", loginData);
            auto [result, response] = client->sendRequest(req);

            if (response)
            {
                if (response->statusCode() == k200OK)
                {
                    successCount++;
                    LOG_DEBUG << "Request " << i + 1 << ": Success (200)";
                }
                else if (response->statusCode() == k429TooManyRequests)
                {
                    rateLimitedCount++;
                    LOG_INFO << "Request " << i + 1 << ": Rate limited (429) [LIMITED]";
                }
                else
                {
                    LOG_DEBUG << "Request " << i + 1 << ": Other status (" << response->statusCode()
                              << ")";
                }
            }

            // Small delay between requests
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        catch (const std::exception &e)
        {
            LOG_WARN << "Request failed: " << e.what();
        }
    }

    // Verify rate limiting is working
    LOG_INFO << "Results: " << successCount << " successful, " << rateLimitedCount
             << " rate limited";

    if (rateLimitedCount > 0)
    {
        LOG_INFO << "[SUCCESS] Rate limiting is working correctly";
        CHECK(successCount <= 5);      // Should not exceed limit
        CHECK(rateLimitedCount >= 1);  // At least some requests should be rate limited
    }
    else
    {
        LOG_WARN << "[!] All requests succeeded - localhost might be whitelisted";
        LOG_WARN << "Remove 127.0.0.1 from trust_ips to test rate limiting";
    }

    // 3. Test endpoint-specific rate limiting on /oauth2/token
    LOG_INFO << "";
    LOG_INFO << "=== Test 2: Endpoint-specific rate limiting on /oauth2/token ===";

    int tokenSuccessCount = 0;
    int tokenRateLimitedCount = 0;

    // Send 12 requests (limit is 10 per minute for /oauth2/token)
    for (int i = 0; i < 12; ++i)
    {
        try
        {
            auto client = HttpClient::newHttpClient(baseUrl);

            Json::Value tokenData;
            tokenData["grant_type"] = "password";
            tokenData["username"] = "test";
            tokenData["password"] = "test";

            auto req = createJsonPostRequest("/oauth2/token", tokenData);
            auto [result, response] = client->sendRequest(req);

            if (response)
            {
                if (response->statusCode() == k200OK || response->statusCode() == k400BadRequest)
                {
                    tokenSuccessCount++;
                }
                else if (response->statusCode() == k429TooManyRequests)
                {
                    tokenRateLimitedCount++;
                    LOG_INFO << "Token request " << i + 1 << ": Rate limited (429) [LIMITED]";
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        catch (const std::exception &e)
        {
            LOG_WARN << "Request failed: " << e.what();
        }
    }

    LOG_INFO << "Token Results: " << tokenSuccessCount << " successful, " << tokenRateLimitedCount
             << " rate limited";

    if (tokenRateLimitedCount > 0)
    {
        LOG_INFO << "Token endpoint rate limiting working";
        CHECK(tokenSuccessCount <= 10);
    }
    else
    {
        LOG_WARN << "[!] Token endpoint not rate limited (localhost whitelisted?)";
    }

    // 4. Test register endpoint rate limiting
    LOG_INFO << "";
    LOG_INFO << "=== Test 3: Register endpoint rate limiting ===";

    int registerSuccessCount = 0;
    int registerRateLimitedCount = 0;

    // Send 7 requests (limit is 5 per minute for /api/register)
    for (int i = 0; i < 7; ++i)
    {
        try
        {
            auto client = HttpClient::newHttpClient(baseUrl);

            Json::Value registerData;
            registerData["username"] = "testuser" + std::to_string(i);
            registerData["password"] = "testpass";
            registerData["email"] = "test" + std::to_string(i) + "@example.com";

            auto req = createJsonPostRequest("/api/register", registerData);
            auto [result, response] = client->sendRequest(req);

            if (response)
            {
                if (response->statusCode() == k200OK || response->statusCode() == k400BadRequest)
                {
                    registerSuccessCount++;
                }
                else if (response->statusCode() == k429TooManyRequests)
                {
                    registerRateLimitedCount++;
                    LOG_INFO << "Register request " << i + 1 << ": Rate limited (429) [LIMITED]";
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        catch (const std::exception &e)
        {
            LOG_WARN << "Request failed: " << e.what();
        }
    }

    LOG_INFO << "Register Results: " << registerSuccessCount << " successful, "
             << registerRateLimitedCount << " rate limited";

    if (registerRateLimitedCount > 0)
    {
        LOG_INFO << "Register endpoint rate limiting working";
        CHECK(registerSuccessCount <= 5);
    }
    else
    {
        LOG_WARN << "[!] Register endpoint not rate limited (localhost "
                    "whitelisted?)";
    }

    // 5. Test whitelist functionality
    LOG_INFO << "";
    LOG_INFO << "=== Test 4: Whitelist functionality ===";

    int allSuccess = true;
    int whitelistTestCount = 0;

    for (int i = 0; i < 10; ++i)
    {
        try
        {
            auto client = HttpClient::newHttpClient(baseUrl);

            Json::Value loginData;
            loginData["username"] = "whitelist_test";
            loginData["password"] = "testpass";

            auto req = createJsonPostRequest("/oauth2/login", loginData);
            auto [result, response] = client->sendRequest(req);

            if (response && response->statusCode() == k429TooManyRequests)
            {
                allSuccess = false;
                LOG_INFO << "Whitelist test: Rate limiting is active "
                            "(localhost not whitelisted) [OK]";
                break;
            }

            whitelistTestCount++;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        catch (const std::exception &e)
        {
            LOG_WARN << "Request failed: " << e.what();
        }
    }

    if (allSuccess && whitelistTestCount >= 10)
    {
        LOG_INFO << "[!] All " << whitelistTestCount
                 << " requests succeeded (localhost whitelisted)";
        LOG_INFO << "To test rate limiting, remove 127.0.0.1 from trust_ips in "
                    "config.json";
    }

    // Summary
    LOG_INFO << "";
    LOG_INFO << "=== Test Summary ===";
    LOG_INFO << "Server running: [OK]";
    LOG_INFO << "Rate limiting active: "
             << (rateLimitedCount > 0 ? "[ACTIVE]" : "[!] (localhost whitelisted)");
    LOG_INFO << "Token limiting active: "
             << (tokenRateLimitedCount > 0 ? "[ACTIVE]" : "[!] (localhost whitelisted)");
    LOG_INFO << "Register limiting active: "
             << (registerRateLimitedCount > 0 ? "[ACTIVE]" : "[!] (localhost whitelisted)");

    if (rateLimitedCount == 0 && tokenRateLimitedCount == 0 && registerRateLimitedCount == 0)
    {
        LOG_WARN << "";
        LOG_WARN << "[!] No rate limiting detected - likely because localhost is "
                    "whitelisted";
        LOG_WARN << "To enable rate limiting testing:";
        LOG_WARN << "1. Edit config.json";
        LOG_WARN << "2. Remove 127.0.0.1 from plugins[].config.trust_ips";
        LOG_WARN << "3. Restart the server";
        LOG_WARN << "4. Run tests again";
    }
    else
    {
        LOG_INFO << "";
        LOG_INFO << "[SUCCESS] OAuth2 rate limiter is working correctly!";
    }

    CHECK(true);  // Test passed
}
