#include <drogon/drogon.h>
#include <vector>
#include <string>
#include <algorithm>

using namespace drogon;

void setupCors()
{
    // Define the whitelist check logic
    auto isAllowed = [](const std::string &origin) -> bool {
        if (origin.empty()) return false;
        
        const auto &customConfig = drogon::app().getCustomConfig();
        const auto &allowOrigins = customConfig["cors"]["allow_origins"];
        
        if (allowOrigins.isArray()) {
            for (const auto &allowed : allowOrigins) {
                if (allowed.asString() == origin) return true;
            }
        }
        return false;
    };

    // Register sync advice to handle CORS preflight (OPTIONS) requests
    drogon::app().registerSyncAdvice([isAllowed](const drogon::HttpRequestPtr &req)
                                         -> drogon::HttpResponsePtr {
        if (req->method() == drogon::HttpMethod::Options)
        {
            const auto &origin = req->getHeader("Origin");
            if (isAllowed(origin))
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->addHeader("Access-Control-Allow-Origin", origin);
                
                const auto &requestMethod = req->getHeader("Access-Control-Request-Method");
                if (!requestMethod.empty())
                {
                    resp->addHeader("Access-Control-Allow-Methods", requestMethod);
                }

                resp->addHeader("Access-Control-Allow-Credentials", "true");

                const auto &requestHeaders = req->getHeader("Access-Control-Request-Headers");
                if (!requestHeaders.empty())
                {
                    resp->addHeader("Access-Control-Allow-Headers", requestHeaders);
                }
                return resp;
            }
        }
        return {};
    });

    // Register post-handling advice to add CORS headers to all responses
    drogon::app().registerPostHandlingAdvice(
        [isAllowed](const drogon::HttpRequestPtr &req,
                    const drogon::HttpResponsePtr &resp) {
            const auto &origin = req->getHeader("Origin");
            if (isAllowed(origin))
            {
                resp->addHeader("Access-Control-Allow-Origin", origin);
                resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
                resp->addHeader("Access-Control-Allow-Credentials", "true");
            }
        });
}

int main() {
    //Set HTTP listener address and port
    drogon::app().addListener("0.0.0.0", 5556);
    
    //Load config file - important to do this BEFORE setupCors if setupCors uses getCustomConfig()
    //Actually, getCustomConfig() is populated after loadConfigFile()
    drogon::app().loadConfigFile("../config.json");
    
    // Setup CORS support
    setupCors();

    drogon::app().run();
    return 0;
}
