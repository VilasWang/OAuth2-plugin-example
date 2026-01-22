#include "RateLimiterFilter.h"
#include <drogon/drogon.h>
#include <drogon/nosql/RedisClient.h>

using namespace drogon;

void RateLimiterFilter::doFilter(const HttpRequestPtr &req,
                                 FilterCallback &&fcb,
                                 FilterChainCallback &&fcc)
{
    // 1. Get Client IP
    std::string clientIp = req->getHeader("X-Forwarded-For");
    if (clientIp.empty())
    {
        clientIp = req->getHeader("X-Real-IP");
    }
    if (clientIp.empty())
    {
        clientIp = req->peerAddr().toIp();
    }
    else
    {
        size_t commaPos = clientIp.find(',');
        if (commaPos != std::string::npos)
        {
            clientIp = clientIp.substr(0, commaPos);
        }
    }

    // 2. Determine Limit based on Path
    int limit = 60;
    std::string path = req->path();

    if (path == "/oauth2/login")
        limit = 5;
    else if (path == "/oauth2/token")
        limit = 10;
    else if (path == "/api/register")
        limit = 5;

    // 3. Redis Rate Limiting
    try
    {
        auto redis = drogon::app().getRedisClient("default");
        if (!redis)
        {
            LOG_ERROR << "Redis client 'default' not found in RateLimiter";
            fcc();  // Fail open
            return;
        }

        std::string key = "rate_limit:" + clientIp + ":" + path;

        // Capture shared_ptr to redis to keep it alive? Client is usually long
        // lived. Use INCR
        redis->execCommandAsync(
            [limit, fcb, fcc, clientIp, path, redis, key](
                const drogon::nosql::RedisResult &r) {
                if (r.type() == drogon::nosql::RedisResultType::kInteger)
                {
                    long long count = r.asInteger();
                    if (count == 1)
                    {
                        // Set expiration for 60 seconds
                        redis->execCommandAsync([](const auto &) {},
                                                [](const std::exception &) {},
                                                "EXPIRE",
                                                key,
                                                "60");
                    }

                    if (count > limit)
                    {
                        LOG_WARN << "Rate Limit Exceeded (Redis): " << clientIp
                                 << " -> " << path << " (" << count << "/"
                                 << limit << ")";
                        auto resp = HttpResponse::newHttpResponse();
                        resp->setStatusCode(k429TooManyRequests);
                        resp->setBody("Too Many Requests");
                        fcb(resp);
                        return;
                    }
                }
                else
                {
                    LOG_ERROR
                        << "Redis RateLimit Error: Unexpected result type";
                }
                fcc();
            },
            [fcc](const std::exception &e) {
                LOG_ERROR << "Redis RateLimit Exception: " << e.what();
                fcc();  // Fail open on Redis error
            },
            "INCRBY",
            key,
            "1");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "RateLimiterFilter Exception: " << e.what();
        fcc();
    }
}
