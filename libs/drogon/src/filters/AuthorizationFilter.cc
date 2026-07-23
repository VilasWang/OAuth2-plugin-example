#include <authforge/drogon/filters/AuthorizationFilter.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <oauth2/error/ErrorResponder.h>
#include <oauth2/error/ErrorTypes.h>
#include <oauth2/error/RequestId.h>
#include <drogon/drogon.h>

namespace authforge::drogon::filters
{

AuthorizationFilter::AuthorizationFilter()
{
}

void AuthorizationFilter::loadConfig()
{
    // Defect 1.4 fix: thread-safe, exactly-once initialization. std::call_once
    // provides its own efficient fast path, so the previous non-atomic
    // `if (initialized_) return;` check-then-act fast path is removed entirely
    // (that read raced with the writes inside the init body).
    std::call_once(initFlag_, [this] { loadRulesSafely(); });
}

void AuthorizationFilter::loadRulesSafely()
{
    // Strong exception guarantee: build the complete result in LOCAL vectors,
    // then commit by swapping into the members only after everything succeeded.
    // std::regex(pattern) may throw std::regex_error and push_back may throw; if
    // we mutated rules_/publicPaths_ directly and threw mid-build, std::call_once
    // would (correctly) NOT consume the flag, leaving the members partially
    // filled and causing duplicate appends on the next retry. Building locally
    // and swapping avoids both partial fill and duplicate-append.
    std::vector<RbacRule> localRules;
    std::vector<std::regex> localPublic;

    auto config = ::drogon::app().getCustomConfig();
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
            localRules.push_back(rule);
            LOG_DEBUG << "RBAC Rule Loaded: " << pattern << " -> " << rule.allowedRoles.size()
                      << " roles";
        }
    }
    // Load public paths (no auth required)
    if (config.isMember("public_paths") && config["public_paths"].isArray())
    {
        for (const auto &path : config["public_paths"])
        {
            localPublic.push_back(std::regex(path.asString()));
            LOG_DEBUG << "Public path loaded: " << path.asString();
        }
    }

    // Commit atomically (w.r.t. exceptions): only reached when the full build
    // succeeded, so the members are never left in a partially-filled state.
    rules_.swap(localRules);
    publicPaths_.swap(localPublic);
}

void AuthorizationFilter::doFilter(
  const ::drogon::HttpRequestPtr &req,
  ::drogon::FilterCallback &&fcb,
  ::drogon::FilterChainCallback &&fccb
)
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
        // Session not found or invalid - use Error Envelope (Req 7.1/7.3)
        LOG_WARN << "Authorization failed: token missing";
        auto error = ::authforge::common::error::Error::fromCode(
          "AUTH_TOKEN_INVALID", ::authforge::common::error::RequestId::resolve(req)
        );
        error.message = "Authentication required";
        auto resp = ::authforge::common::error::ErrorResponder::buildResponse(req, error);
        fcb(resp);  // Return response -> Use fcb
        return;
    }

    // 2. Validate Token
    auto plugin = ::drogon::app().getPlugin<OAuth2Plugin>();
    if (!plugin)
    {
        LOG_ERROR << "OAuth2Plugin not found!";
        auto error = ::authforge::common::error::Error::fromCode(
          "INTERNAL_ERROR", ::authforge::common::error::RequestId::resolve(req)
        );
        error.message = "OAuth2 plugin not available";
        auto resp = ::authforge::common::error::ErrorResponder::buildResponse(req, error);
        fcb(resp);  // Return response -> Use fcb
        return;
    }

    // Wrap callbacks to avoid move/copy issues in nested lambdas
    // FilterCallback (Arg 2) = Return Response (Stop/Deny)
    // FilterChainCallback (Arg 3) = Continue (Pass)
    auto denyCbPtr = std::make_shared<::drogon::FilterCallback>(std::move(fcb));
    auto nextCbPtr = std::make_shared<::drogon::FilterChainCallback>(std::move(fccb));

    plugin->validateAccessToken(
      token,
      [this, req, denyCbPtr, nextCbPtr, plugin](
        std::shared_ptr<OAuth2Plugin::AccessToken> at
      ) mutable {
          if (!at)
          {
              LOG_WARN << "Authorization failed: invalid or expired token";
              auto error = ::authforge::common::error::Error::fromCode(
                "AUTH_TOKEN_INVALID", ::authforge::common::error::RequestId::resolve(req)
              );
              error.message = "Invalid or expired token";
              auto resp = ::authforge::common::error::ErrorResponder::buildResponse(req, error);
              (*denyCbPtr)(resp);
              return;
          }

          // 3. Get User Roles
          plugin->getUserRoles(
            at->userId, [this, req, denyCbPtr, nextCbPtr](std::vector<std::string> roles) mutable {
                // 4. Check Access
                if (checkAccess(roles, req->path()))
                {
                    (*nextCbPtr)();  // ALLOW -> Continue
                }
                else
                {
                    LOG_WARN << "Authorization failed: insufficient permissions for path "
                             << req->path();
                    auto error = ::authforge::common::error::Error::fromCode(
                      "AUTHZ_INSUFFICIENT_PERMISSIONS",
                      ::authforge::common::error::RequestId::resolve(req)
                    );
                    error.message = "Insufficient permissions";
                    auto resp =
                      ::authforge::common::error::ErrorResponder::buildResponse(req, error);
                    (*denyCbPtr)(resp);  // DENY -> Return 403
                }
            }
          );
      }
    );
}

bool AuthorizationFilter::checkAccess(
  const std::vector<std::string> &userRoles,
  const std::string &path
)
{
    // Check public paths first (no auth required)
    for (const auto &publicPath : publicPaths_)
    {
        if (std::regex_match(path, publicPath))
            return true;
    }

    // Check RBAC rules
    for (const auto &rule : rules_)
    {
        if (std::regex_match(path, rule.pathPattern))
        {
            // Rule matched - check if user has any of the allowed roles
            for (const auto &allowed : rule.allowedRoles)
            {
                for (const auto &userRole : userRoles)
                {
                    if (userRole == allowed)
                        return true;
                }
            }
            // Rule matched but roles didn't -> DENY
            return false;
        }
    }

    // DEFAULT DENY: no rule matched and not in public_paths
    return false;
}

}  // namespace authforge::drogon::filters
