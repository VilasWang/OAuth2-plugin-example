#pragma once

#include <drogon/HttpFilter.h>
#include <drogon/drogon.h>
#include <string>
#include <vector>
#include <regex>

using namespace drogon;

class AuthorizationFilter : public HttpFilter<AuthorizationFilter>
{
  public:
    AuthorizationFilter();
    void doFilter(const HttpRequestPtr &req,
                  FilterCallback &&fcb,
                  FilterChainCallback &&fccb) override;

  private:
    // Path regex -> Allowed Roles
    struct RbacRule
    {
        std::regex pathPattern;
        std::vector<std::string> allowedRoles;
    };

    std::vector<RbacRule> rules_;
    bool initialized_ = false;

    void loadConfig();
    bool checkAccess(const std::vector<std::string> &userRoles,
                     const std::string &path);
};
