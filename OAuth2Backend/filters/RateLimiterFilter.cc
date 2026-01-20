#include "RateLimiterFilter.h"
#include <drogon/drogon.h>
#include <mutex>
#include <map>
#include <chrono>

using namespace drogon;

// Simple in-memory rate limiter for demonstration
// In production, use Redis for distributed limiting
namespace {
    std::map<std::string, int> requestCounts;
    std::string currentMinute;
    std::mutex limitMutex;

    void cleanupOldCounts(const std::string& nowMinute) {
        if (currentMinute != nowMinute) {
            requestCounts.clear();
            currentMinute = nowMinute;
        }
    }
}

void RateLimiterFilter::doFilter(const HttpRequestPtr &req,
                                 FilterCallback &&fcb,
                                 FilterChainCallback &&fcc)
{
    // 1. Get Client IP
    auto clientIp = req->peerAddr().toIp();
    
    // 2. Determine Limit based on Path
    int limit = 60; // Default
    std::string path = req->path();
    
    if (path == "/oauth2/login") limit = 5;
    else if (path == "/oauth2/token") limit = 10;
    else if (path == "/api/register") limit = 5;

    // 3. Get Current Minute
    auto now = std::chrono::system_clock::now();
    time_t now_c = std::chrono::system_clock::to_time_t(now);
    struct tm *parts = std::localtime(&now_c);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", parts);
    std::string nowMinute(buf);

    // 4. Check Limit
    {
        std::lock_guard<std::mutex> lock(limitMutex);
        cleanupOldCounts(nowMinute); // Reset if new minute
        
        std::string key = clientIp + ":" + path;
        int count = ++requestCounts[key];
        
        if (count > limit) {
            LOG_WARN << "Rate Limit Exceeded: " << clientIp << " -> " << path << " (" << count << "/" << limit << ")";
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k429TooManyRequests);
            resp->setBody("Too Many Requests");
            fcb(resp);
            return;
        }
    }

    // Pass
    fcc();
}
