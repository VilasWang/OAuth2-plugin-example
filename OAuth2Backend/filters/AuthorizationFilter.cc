#include "AuthorizationFilter.h"
#include "plugins/OAuth2Plugin.h"
#include <drogon/drogon.h>

using namespace drogon;

AuthorizationFilter::AuthorizationFilter()
{
}

void AuthorizationFilter::loadConfig()
{
    if (initialized_)
        return;
    auto config = app().getCustomConfig();
    if (config.isMember("rbac_rules") && config["rbac_rules"].isObject())
    {
        auto rules = config["rbac_rules"];
        for (auto it = rules.begin(); it != rules.end(); ++it)
        {
            std::string pattern = it.name();
            RbacRule rule;
            rule.pathPattern = std::regex(pattern);

            auto rolesJson = *it;
            if (rolesJson.isArray())
            {
                for (const auto &role : rolesJson)
                {
                    rule.allowedRoles.push_back(role.asString());
                }
            }
            rules_.push_back(rule);
            LOG_INFO << "RBAC Rule Loaded: " << pattern << " -> "
                     << rule.allowedRoles.size() << " roles";
        }
    }
    initialized_ = true;
}

void AuthorizationFilter::doFilter(const HttpRequestPtr &req,
                                   FilterCallback &&fcb,
                                   FilterChainCallback &&fccb)
{
    loadConfig();

    // 1. Extract Token
    std::string token;
    auto authHeader = req->getHeader("Authorization");
    if (!authHeader.empty() && authHeader.find("Bearer ") == 0)
    {
        token = authHeader.substr(7);
    }
    else
    {
        token = req->getParameter("access_token");
    }

    if (token.empty())
    {
        Json::Value error;
        error["error"] = "unauthorized";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k401Unauthorized);
        fcb(resp);  // Return response -> Use fcb
        return;
    }

    // 2. Validate Token
    auto plugin = app().getPlugin<OAuth2Plugin>();
    if (!plugin)
    {
        LOG_ERROR << "OAuth2Plugin not found!";
        fcb(HttpResponse::newHttpResponse(
            k500InternalServerError,
            ContentType::CT_TEXT_PLAIN));  // Return response -> Use fcb
        return;
    }

    // Wrap callbacks to avoid move/copy issues in nested lambdas
    // FilterCallback (Arg 2) = Return Response (Stop/Deny)
    // FilterChainCallback (Arg 3) = Continue (Pass)
    auto denyCbPtr = std::make_shared<FilterCallback>(std::move(fcb));
    auto nextCbPtr = std::make_shared<FilterChainCallback>(std::move(fccb));

    plugin->validateAccessToken(
        token,
        [this, req, denyCbPtr, nextCbPtr, plugin](
            std::shared_ptr<OAuth2Plugin::AccessToken> at) mutable {
            if (!at)
            {
                Json::Value error;
                error["error"] = "invalid_token";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k401Unauthorized);
                (*denyCbPtr)(resp);
                return;
            }

            // 3. Get User Roles
            plugin->getUserRoles(
                at->userId,
                [this, req, denyCbPtr, nextCbPtr](
                    std::vector<std::string> roles) mutable {
                    // 4. Check Access
                    if (checkAccess(roles, req->path()))
                    {
                        (*nextCbPtr)();  // ALLOW -> Continue
                    }
                    else
                    {
                        Json::Value error;
                        error["error"] = "forbidden";
                        error["message"] = "Insufficient permissions";
                        auto resp = HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(k403Forbidden);
                        (*denyCbPtr)(resp);  // DENY -> Return 403
                    }
                });
        });
}

bool AuthorizationFilter::checkAccess(const std::vector<std::string> &userRoles,
                                      const std::string &path)
{
    // If no rules match, DENY by default if config exists?
    // Or ALLOW by default?
    // Security Best Practice: Deny by default.
    // However, this filter is likely applied globally or specifically.
    // If applied specifically to a route, there MUST be a rule for it.
    // But if we rely on regex, we might match multiple.

    bool matchedAnyRule = false;

    for (const auto &rule : rules_)
    {
        if (std::regex_match(path, rule.pathPattern))
        {
            matchedAnyRule = true;
            // Check if user has ANY of the allowed roles
            for (const auto &allowed : rule.allowedRoles)
            {
                for (const auto &userRole : userRoles)
                {
                    if (userRole == allowed)
                        return true;
                }
            }
        }
    }

    // If path matched a rule but roles didn't match -> FALSE (Implicit Deny)
    // If path didn't match ANY rule -> TRUE (Pass-through, assume protected by
    // other means or public) Rationale: If I add this filter globally, I don't
    // want to block login/public pages. If I add this filter to a controller, I
    // expect a rule to exist. Let's go with: If NO rule matches, ALLOW
    // (Public). If Rule matches, Enforce.
    if (!matchedAnyRule)
        return true;

    return false;
}
