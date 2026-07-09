#pragma once

// M3 Task 20 slice 1 (authforge-sdk-refactor): relocated verbatim from
// OAuth2Plugin/include/oauth2/filters/RequestValidationFilter.h into
// authforge::drogon::filters. Zero dependency on the OAuth2Plugin
// singleton (drogon::app().getPlugin<OAuth2Plugin>()) -- it only reads
// its own static rule table and the request object -- so this is grouped
// with the validation/ classes as the lowest-risk first slice of the M3
// Drogon-binding migration (see libs/drogon/CMakeLists.txt's header
// comment for the full rationale).

#include <drogon/HttpFilter.h>
#include <drogon/drogon.h>
#include <string>
#include <vector>
#include <map>
#include <authforge/drogon/validation/HttpResponder.h>
#include <authforge/drogon/validation/RuleSet.h>

namespace authforge::drogon::filters
{

/**
 * @brief RequestValidationFilter - 自动验证HTTP请求的Filter
 *
 * 在Controller执行前自动验证请求参数，处理基础、通用的验证规则：
 * - 格式验证（长度、字符集）
 * - 必填字段检查
 * - OAuth2专用验证
 */
class RequestValidationFilter : public ::drogon::HttpFilter<RequestValidationFilter>
{
  public:
    RequestValidationFilter() = default;
    ~RequestValidationFilter() override = default;

    void doFilter(
      const ::drogon::HttpRequestPtr &req,
      ::drogon::FilterCallback &&fcb,
      ::drogon::FilterChainCallback &&fccb
    ) override;

  private:
    // 定义路由验证规则
    struct RouteValidationRules
    {
        std::vector<authforge::drogon::validation::Rule> rules;
        bool enabled;
    };

    // 获取路由的验证规则
    RouteValidationRules getValidationRules(const std::string &path) const;

    // OAuth2 路由验证规则 —— 函数内静态访问器（Meyers Singleton）。
    // C++11 起函数局部静态的首次初始化线程安全且仅执行一次，既消除文件作用域
    // 全局对象的跨翻译单元构造次序依赖（SIOF），又保留"一次性填充"语义。
    static const std::map<std::string, RouteValidationRules> &rules();

    // 构建完整的验证规则集（合并构造与一次性填充）
    static std::map<std::string, RouteValidationRules> buildRules();
};

}  // namespace authforge::drogon::filters
