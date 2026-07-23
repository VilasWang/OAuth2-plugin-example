#include <oauth2/filters/RequestValidationFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <string>

using namespace authforge::drogon::validation;

// 构建完整的验证规则集（合并构造与一次性填充）。
// 返回完整 map，供函数内静态访问器 rules() 首次访问时初始化。
std::map<std::string, RequestValidationFilter::RouteValidationRules> RequestValidationFilter::
  buildRules()
{
    std::map<std::string, RouteValidationRules> rules;

    // /oauth2/authorize 验证规则
    rules["/oauth2/authorize"] = {
      {{"client_id", "query", true, 1, 128, CLIENT_ID_PATTERN, nullptr},
       {"redirect_uri", "query", true, 10, 2048, REDIRECT_URI_PATTERN, nullptr},
       {"response_type", "query", true, 1, 10, RESPONSE_TYPE_PATTERN, nullptr}},
      true  // enabled
    };

    // /oauth2/token 验证规则
    rules["/oauth2/token"] = {
      {{"grant_type", "body", true, 1, 50, GRANT_TYPE_PATTERN, nullptr}},
      true  // enabled
    };

    // /oauth2/login 验证规则
    rules["/oauth2/login"] = {
      {{"username", "body", true, 1, 100, USERNAME_PATTERN, nullptr},
       {"password", "body", true, 8, 200, PASSWORD_PATTERN, nullptr}},
      true  // enabled
    };

    // /api/register 验证规则
    rules["/api/register"] = {
      {{"username", "body", true, 1, 100, USERNAME_PATTERN, nullptr},
       {"password", "body", true, 8, 200, PASSWORD_PATTERN, nullptr}},
      true  // enabled
    };

    return rules;
}

// 函数内静态访问器（Meyers Singleton）。
// C++11 起函数局部静态首次初始化线程安全且仅执行一次：既消除文件作用域
// 全局对象的跨翻译单元构造次序依赖（SIOF），又把"构造 + 一次性填充"合并为
// 标准保证的线程安全初始化（无需独立的 call_once/once_flag）。
const std::map<std::string, RequestValidationFilter::RouteValidationRules> &
RequestValidationFilter::rules()
{
    static const std::map<std::string, RouteValidationRules> kRules = buildRules();
    return kRules;
}

RequestValidationFilter::RouteValidationRules RequestValidationFilter::getValidationRules(
  const std::string &path
) const
{
    const auto &validationRules = rules();

    // 精确匹配
    auto it = validationRules.find(path);
    if (it != validationRules.end())
    {
        return it->second;
    }

    // 模式匹配 (支持路径参数，如 /api/user/:id)
    for (const auto &[routePattern, rules] : validationRules)
    {
        if (path == routePattern)
        {
            return rules;
        }

        // 简单的模式匹配（可以扩展为正则表达式）
        size_t pos = routePattern.find(":");
        if (pos != std::string::npos)
        {
            std::string prefix = routePattern.substr(0, pos);
            if (path.length() > prefix.length() && path.substr(0, prefix.length()) == prefix)
            {
                return rules;
            }
        }
    }

    // 没有找到匹配规则，禁用验证
    return {{}, false};
}

void RequestValidationFilter::doFilter(
  const HttpRequestPtr &req,
  FilterCallback &&fcb,
  FilterChainCallback &&fccb
)
{
    // 获取请求路径
    std::string path = req->path();

    // 获取验证规则
    auto routeRules = getValidationRules(path);

    // 如果没有规则或规则被禁用，直接通过
    if (!routeRules.enabled || routeRules.rules.empty())
    {
        fccb();
        return;
    }

    // 执行验证
    auto errors = RuleSet::validateRequest(req, routeRules.rules);

    // 如果有验证错误，返回错误响应
    if (!errors.empty())
    {
        HttpResponder::respondWithErrors(errors, std::move(fcb));
        return;
    }

    // 验证通过，继续执行
    fccb();
}
