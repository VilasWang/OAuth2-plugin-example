#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <drogon/utils/Utilities.h>
#include <drogon/Cookie.h>
#include "../controllers/OAuth2Controller.h"
#include <future>
#include <iostream>
#include <map>

using namespace drogon;

DROGON_TEST(IntegrationE2E)
{
    // Step 0: Verify DB Connection
    LOG_INFO << "--- Step 0: Verify DB Connection ---";
    auto dbClient = app().getDbClient();
    if (!dbClient)
    {
        LOG_ERROR << "DB Client is NULL! Config likely not loaded correctly.";
        CHECK(dbClient != nullptr);
        return;
    }
    LOG_INFO << "DB Client OK";

    // Direct Controller Integration Testing
    // Bypasses HTTP Routing to avoid static registration linker issues.
    auto ctrl = std::make_shared<OAuth2Controller>();

    // Helper for async controller calls (Form Data)
    auto callCtrlForm =
        [&](std::function<void(const HttpRequestPtr &,
                               std::function<void(const HttpResponsePtr &)> &&)>
                method,
            const std::map<std::string, std::string> &params)
        -> HttpResponsePtr {
        std::promise<HttpResponsePtr> p;
        auto f = p.get_future();

        auto req = HttpRequest::newHttpRequest();
        req->setMethod(Post);
        for (const auto &param : params)
        {
            req->setParameter(param.first, param.second);
        }

        method(req, [&](const HttpResponsePtr &resp) { p.set_value(resp); });

        return f.get();
    };

    // 1. Register User
    LOG_INFO << "--- Step 1: Register User (Direct) ---";
    std::string userId = "direct_user_" + utils::getUuid().substr(0, 8);
    {
        std::map<std::string, std::string> params;
        params["username"] = userId;
        params["password"] = "password123";
        params["email"] = userId + "@example.com";

        // Bind method: OAuth2Controller::registerUser
        // Since it's a member function, we bind 'this' to 'ctrl'.
        auto method = std::bind(&OAuth2Controller::registerUser,
                                ctrl,
                                std::placeholders::_1,
                                std::placeholders::_2);

        auto resp = callCtrlForm(method, params);

        if (resp->getStatusCode() == k409Conflict)
        {
            LOG_WARN << "User already exists, proceeding...";
        }
        else
        {
            if (resp->getStatusCode() != k200OK)
            {
                LOG_ERROR << "Register Failed. Status: "
                          << resp->getStatusCode() << " Body: "
                          << ((resp->getBody().length() > 0)
                                  ? std::string(resp->getBody())
                                  : "Empty");
            }
            CHECK(resp->getStatusCode() == k200OK);
            LOG_INFO << "User Registered: " << userId;
        }
    }

    // 2. Login (POST /oauth2/login)
    // Controller logic for internal login assumes Form Data usually?
    // Let's check OAuth2Controller::login implementation in source if needed.
    // Assuming JSON or Form.
    // Let's skip Login for now and focus on Token Exchange if we can get a
    // Code? But Code generation requires Authorize endpoint which checks
    // Session. Session is attached to Request. If we create a Request, we can
    // attach a Session object? Drogon tests support `req->setSession()`.

    // Cleanup
    if (!userId.empty())
    {
        auto client = app().getDbClient();
        std::promise<void> p;
        auto f = p.get_future();
        client->execSqlAsync(
            "DELETE FROM users WHERE username = $1",
            [&](const drogon::orm::Result &) { p.set_value(); },
            [&](const drogon::orm::DrogonDbException &e) {
                LOG_ERROR << "Integration Cleanup Failed: " << e.base().what();
                p.set_value();
            },
            userId);
        f.get();
        LOG_INFO << "Integration: Cleaned up user " << userId;
    }

    LOG_INFO << "Integration Test Step 1 Complete";
}
