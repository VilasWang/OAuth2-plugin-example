#pragma once

#include <drogon/HttpFilter.h>
#include <drogon/drogon.h>
#include <mutex>
#include <string>
#include <vector>
#include <regex>

using namespace drogon;

class OAuth2Plugin;

namespace fulla::drogon::filters
{

class AuthorizationFilter : public HttpFilter<AuthorizationFilter>
{
  public:
    AuthorizationFilter();

    // M3 Task 23 (fulla-sdk-refactor, evaluation H4 "controller/filter
    // 去单例化"): explicit dependency injection point, set once at startup
    // (bootstrap::wireFilterPluginDependencies(), via
    // drogon::DrClassMap::getSingleInstance<AuthorizationFilter>() after
    // the plugin has been constructed) instead of doFilter() calling
    // drogon::app().getPlugin<OAuth2Plugin>() on every request. Non-owning
    // (see HealthController::setPlugin()'s identical comment on plugin
    // lifetime). doFilter() falls back to the global lookup if unset, so
    // this is additive, not a behavior-changing requirement.
    void setPlugin(::OAuth2Plugin *plugin)
    {
        plugin_ = plugin;
    }

    void doFilter(
      const HttpRequestPtr &req,
      FilterCallback &&fcb,
      FilterChainCallback &&fccb
    ) override;

  private:
    ::OAuth2Plugin *plugin_ = nullptr;
    ::OAuth2Plugin *resolvePlugin() const;

    // Path regex -> Allowed Roles
    struct RbacRule
    {
        std::regex pathPattern;
        std::vector<std::string> allowedRoles;
    };

    std::vector<RbacRule> rules_;
    std::vector<std::regex> publicPaths_;

    // Per-instance, non-static once_flag. MUST NOT be a function-local
    // `static std::once_flag`: a function-local static is shared across ALL
    // instances, but the init body writes this->rules_/this->publicPaths_, so a
    // shared flag would fill only the first instance and leave every other
    // instance with silently-empty rules. A per-instance member flag guarantees
    // each instance's rule-loading body runs exactly once (defect 1.4 fix).
    std::once_flag initFlag_;

    void loadConfig();
    void loadRulesSafely();
    bool checkAccess(const std::vector<std::string> &userRoles, const std::string &path);
};

}  // namespace fulla::drogon::filters
