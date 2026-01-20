#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <drogon/HttpFilter.h>
#include "../filters/RateLimiterFilter.h"
#include <memory>

using namespace drogon;

DROGON_TEST(RateLimitTest)
{
    // Real IP Extraction Test
    {
        auto filter = std::make_shared<RateLimiterFilter>();

        auto req = HttpRequest::newHttpRequest();
        req->setPath("/oauth2/token");
        req->setMethod(drogon::Post);
        req->addHeader("X-Forwarded-For", "10.0.0.1, 10.0.0.2");

        bool blocked = false;

        for (int i = 0; i < 10; ++i)
        {
            filter->doFilter(req, [](const HttpResponsePtr &) {}, []() {});
        }

        // 11th time
        filter->doFilter(
            req,
            [&](const HttpResponsePtr &resp) {
                if (resp->getStatusCode() == k429TooManyRequests)
                    blocked = true;
            },
            []() {});

        CHECK(blocked == true);
    }

    // Different IP Test
    {
        auto filter = std::make_shared<RateLimiterFilter>();
        auto req = HttpRequest::newHttpRequest();
        req->setPath("/oauth2/token");
        req->setMethod(drogon::Post);
        req->addHeader("X-Forwarded-For", "10.0.0.2");  // Different IP

        bool blocked = false;

        // Should pass (count is 0 for this IP)
        filter->doFilter(
            req,
            [&](const HttpResponsePtr &resp) {
                if (resp->getStatusCode() == k429TooManyRequests)
                    blocked = true;
            },
            []() {});
        CHECK(blocked == false);
    }
}
