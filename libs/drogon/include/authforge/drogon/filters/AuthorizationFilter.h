#pragma once

// M3 Task 20 slice 2 (authforge-sdk-refactor): relocated verbatim from
// OAuth2Plugin/include/oauth2/filters/AuthorizationFilter.h into
// authforge::drogon::filters. See OAuth2AuthFilter.h in this directory
// for the rationale on why reaching through the OAuth2Plugin singleton
// is acceptable in this slice.

#include <drogon/HttpFilter.h>
#include <drogon/drogon.h>
#include <mutex>
#include <string>
#include <vector>
#include <regex>

namespace authforge::drogon::filters
{

class AuthorizationFilter : public ::drogon::HttpFilter<AuthorizationFilter>
{
  public:
    AuthorizationFilter();
    void doFilter(
      const ::drogon::HttpRequestPtr &req,
      ::drogon::FilterCallback &&fcb,
      ::drogon::FilterChainCallback &&fccb
    ) override;

  private:
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

}  // namespace authforge::drogon::filters
