#include <authforge/drogon/validation/RuleSet.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <regex>
#include <algorithm>

namespace authforge::drogon::validation
{

std::optional<std::string> RuleSet::validateField(
  const std::string &value,
  const std::string &fieldName,
  const Rule &rule
)
{
    // 检查必填字段
    if (rule.required && value.empty())
    {
        return fieldName + " is required";
    }

    // 如果字段为空且非必填，跳过其他验证
    if (value.empty())
    {
        return std::nullopt;
    }

    // 长度验证
    if (value.length() < rule.minLength)
    {
        return fieldName + " must be at least " + std::to_string(rule.minLength) + " characters";
    }

    if (rule.maxLength > 0 && value.length() > rule.maxLength)
    {
        return fieldName + " must be at most " + std::to_string(rule.maxLength) + " characters";
    }

    // 正则表达式验证
    if (!rule.pattern.empty())
    {
        try
        {
            std::regex pattern(rule.pattern);
            if (!std::regex_match(value, pattern))
            {
                return fieldName + " format is invalid";
            }
        }
        catch (const std::regex_error &)
        {
            // 如果正则表达式无效，记录警告但不阻止验证
            // 这样可以避免配置错误导致整个验证失败
        }
    }

    // 自定义验证器
    if (rule.custom && !rule.custom(value))
    {
        return fieldName + " validation failed";
    }

    return std::nullopt;
}

std::vector<std::string> RuleSet::validateFields(
  const std::vector<std::pair<std::string, std::string>> &fieldsAndValues,
  const std::vector<Rule> &rules
)
{
    std::vector<std::string> errors;

    for (const auto &rule : rules)
    {
        // 查找对应的字段值
        std::string value;
        bool found = false;
        for (const auto &[fieldName, fieldValue] : fieldsAndValues)
        {
            if (fieldName == rule.field)
            {
                value = fieldValue;
                found = true;
                break;
            }
        }

        // 如果字段不存在且非必填，跳过
        if (!found && !rule.required)
        {
            continue;
        }

        // 执行验证
        auto error = validateField(value, rule.field, rule);
        if (error)
        {
            errors.push_back(*error);
        }
    }

    return errors;
}

std::string RuleSet::extractFieldValue(
  const ::drogon::HttpRequestPtr &req,
  const std::string &field,
  const std::string &source
)
{
    if (source == "query")
    {
        return req->getParameter(field);
    }
    else if (source == "body")
    {
        // 尝试从 JSON body 获取
        if (req->contentType() == ::drogon::CT_APPLICATION_JSON)
        {
            auto json = req->getJsonObject();
            if (json)
            {
                return json->get(field, "").asString();
            }
        }
        // 回退到 form 数据
        return req->getParameter(field);
    }
    else if (source == "header")
    {
        return req->getHeader(field);
    }

    return "";
}

std::vector<std::string> RuleSet::validateRequest(
  const ::drogon::HttpRequestPtr &req,
  const std::vector<Rule> &rules
)
{
    std::vector<std::pair<std::string, std::string>> fieldsAndValues;

    // 提取所有需要的字段值
    for (const auto &rule : rules)
    {
        std::string value = extractFieldValue(req, rule.field, rule.source);
        fieldsAndValues.push_back({rule.field, value});
    }

    return validateFields(fieldsAndValues, rules);
}

// OAuth2 专用验证方法实现

std::optional<std::string> RuleSet::validateClientId(const std::string &clientId)
{
    if (clientId.empty())
    {
        return "client_id is required";
    }

    if (clientId.length() > 128)
    {
        return "client_id exceeds maximum length of 128 characters";
    }

    // 使用现有的 Validator
    auto result = RuleEngine::validateClientId(clientId);
    if (!result.ok)
    {
        return result.message;
    }

    return std::nullopt;
}

std::optional<std::string> RuleSet::validateClientSecret(const std::string &secret)
{
    if (secret.empty())
    {
        // client_secret 可以为空（公共客户端）
        return std::nullopt;
    }

    if (secret.length() > 256)
    {
        return "client_secret exceeds maximum length of 256 characters";
    }

    auto result = RuleEngine::validateClientSecret(secret);
    if (!result.ok)
    {
        return result.message;
    }

    return std::nullopt;
}

std::optional<std::string> RuleSet::validateRedirectUri(const std::string &uri)
{
    if (uri.empty())
    {
        return "redirect_uri is required";
    }

    auto result = RuleEngine::validateRedirectUri(uri);
    if (!result.ok)
    {
        return result.message;
    }

    return std::nullopt;
}

std::optional<std::string> RuleSet::validateBackchannelLogoutUri(const std::string &uri)
{
    // Empty == "not configured": valid (the notifier skips clients without a
    // backchannel_logout_uri).
    if (uri.empty())
        return std::nullopt;

    // OIDC Back-Channel Logout 1.0 §2.3: the backchannel_logout_uri MUST use
    // https. The auth.allow_http_redirect_uri dev hatch is honored for parity
    // with redirect_uri validation; loopback IP literals are NOT exempt here
    // (this is server-to-server delivery, so RFC 8252's loopback rationale
    // does not apply).
    if (uri.rfind("https://", 0) == 0)
        return std::nullopt;
    if (uri.rfind("http://", 0) == 0)
    {
        const auto &cfg = ::drogon::app().getCustomConfig();
        if (cfg.isMember("auth") && cfg["auth"].isMember("allow_http_redirect_uri") &&
            cfg["auth"]["allow_http_redirect_uri"].asBool())
        {
            return std::nullopt;
        }
    }
    return std::string{"backchannel_logout_uri must use https"};
}

std::optional<std::string> RuleSet::validateScope(const std::string &scope)
{
    // scope 是可选的
    if (scope.empty())
    {
        return std::nullopt;
    }

    auto result = RuleEngine::validateScope(scope);
    if (!result.ok)
    {
        return result.message;
    }

    return std::nullopt;
}

std::optional<std::string> RuleSet::validateResponseType(const std::string &type)
{
    if (type.empty())
    {
        return "response_type is required";
    }

    if (type != "code")
    {
        return "response_type must be 'code'";
    }

    return std::nullopt;
}

std::optional<std::string> RuleSet::validateGrantType(const std::string &type)
{
    if (type.empty())
    {
        return "grant_type is required";
    }

    if (
      type != "authorization_code" && type != "refresh_token" && type != "client_credentials" &&
      type != "urn:ietf:params:oauth:grant-type:device_code"
    )
    {
        return "grant_type must be 'authorization_code', 'refresh_token', "
               "'client_credentials', or 'urn:ietf:params:oauth:grant-type:device_code'";
    }

    return std::nullopt;
}

std::optional<std::string> RuleSet::validateToken(const std::string &token)
{
    if (token.empty())
    {
        return "token is required";
    }

    auto result = RuleEngine::validateToken(token);
    if (!result.ok)
    {
        return result.message;
    }

    return std::nullopt;
}

// 便捷验证组合方法

std::vector<std::string> RuleSet::oauth2Authorize(const ::drogon::HttpRequestPtr &req)
{
    std::vector<std::string> errors;

    // 验证 client_id
    auto clientId = req->getParameter("client_id");
    auto error1 = validateClientId(clientId);
    if (error1)
    {
        errors.push_back(*error1);
    }

    // 验证 redirect_uri
    auto redirectUri = req->getParameter("redirect_uri");
    auto error2 = validateRedirectUri(redirectUri);
    if (error2)
    {
        errors.push_back(*error2);
    }

    // 验证 response_type
    auto responseType = req->getParameter("response_type");
    auto error3 = validateResponseType(responseType);
    if (error3)
    {
        errors.push_back(*error3);
    }

    // 验证 scope (可选)
    auto scope = req->getParameter("scope");
    if (!scope.empty())
    {
        auto error4 = validateScope(scope);
        if (error4)
        {
            errors.push_back(*error4);
        }
    }

    return errors;
}

std::vector<std::string> RuleSet::oauth2Token(const ::drogon::HttpRequestPtr &req)
{
    std::vector<std::string> errors;

    // 优先从 POST body 获取参数
    std::string grantType, code, clientId, redirectUri, refreshToken;

    if (req->method() == ::drogon::Post)
    {
        auto params = req->getParameters();
        grantType = params["grant_type"];
        code = params["code"];
        clientId = params["client_id"];
        redirectUri = params["redirect_uri"];
        refreshToken = params["refresh_token"];
    }
    else
    {
        grantType = req->getParameter("grant_type");
        code = req->getParameter("code");
        clientId = req->getParameter("client_id");
        redirectUri = req->getParameter("redirect_uri");
        refreshToken = req->getParameter("refresh_token");
    }

    // 验证 grant_type
    auto error1 = validateGrantType(grantType);
    if (error1)
    {
        errors.push_back(*error1);
    }

    // 根据 grant_type 验证其他参数
    if (grantType == "authorization_code")
    {
        auto error2 = validateToken(code);
        if (error2)
        {
            errors.push_back("code: " + *error2);
        }

        if (!clientId.empty())
        {
            auto error3 = validateClientId(clientId);
            if (error3)
            {
                errors.push_back(*error3);
            }
        }
    }
    else if (grantType == "refresh_token")
    {
        auto error4 = validateToken(refreshToken);
        if (error4)
        {
            errors.push_back("refresh_token: " + *error4);
        }
    }

    return errors;
}

std::vector<std::string> RuleSet::login(const ::drogon::HttpRequestPtr &req)
{
    std::vector<std::string> errors;

    // 优先从 POST body 获取参数
    // username 字段语义为"登录标识"（email 或 username），保持 API 向后兼容
    std::string identifier, password;

    if (req->contentType() == ::drogon::CT_APPLICATION_JSON)
    {
        auto json = req->getJsonObject();
        if (json)
        {
            identifier = json->get("username", "").asString();
            password = json->get("password", "").asString();
        }
    }
    else
    {
        auto params = req->getParameters();
        identifier = params["username"];
        password = params["password"];
    }

    // 验证登录标识（email 或 username，至少一个非空）
    if (identifier.empty())
    {
        errors.push_back("username is required");
    }
    else
    {
        // 长度上限：取 username(100) 与 email(254) 的较大者
        if (identifier.length() > EMAIL_MAX_LEN)
        {
            errors.push_back(
              "username exceeds maximum length of " + std::to_string(EMAIL_MAX_LEN) + " characters"
            );
        }
        // 若标识形如 email（含 @），校验格式，避免无效 email 进入查询
        if (identifier.find('@') != std::string::npos)
        {
            try
            {
                std::regex re(EMAIL_PATTERN);
                if (!std::regex_match(identifier, re))
                {
                    errors.push_back("email format is invalid");
                }
            }
            catch (const std::regex_error &)
            {
                // 正则编译失败不应阻塞请求，降级为仅长度校验
            }
        }
    }

    // 验证 password
    if (password.empty())
    {
        errors.push_back("password is required");
    }
    else if (password.length() > 200)
    {
        errors.push_back("password exceeds maximum length of 200 characters");
    }

    return errors;
}

std::vector<std::string> RuleSet::registerUser(const ::drogon::HttpRequestPtr &req)
{
    std::vector<std::string> errors;

    std::string username, password, email;

    if (req->contentType() == ::drogon::CT_APPLICATION_JSON)
    {
        auto json = req->getJsonObject();
        if (json)
        {
            username = json->get("username", "").asString();
            password = json->get("password", "").asString();
            email = json->get("email", "").asString();
        }
    }
    else
    {
        auto params = req->getParameters();
        username = params["username"];
        password = params["password"];
        email = params["email"];
    }

    // 验证 username（可选字段：非空时须同时满足长度与字符集。
    // USERNAME_PATTERN 不含 @，强制该约束可防止含 @ 的 username 在登录
    // 分流（identifier.find('@')）时被误判为 email 而永远无法登录）
    if (!username.empty())
    {
        if (username.length() > 100)
        {
            errors.push_back("username exceeds maximum length of 100 characters");
        }
        else
        {
            try
            {
                std::regex re(USERNAME_PATTERN);
                if (!std::regex_match(username, re))
                {
                    errors.push_back("username format is invalid");
                }
            }
            catch (const std::regex_error &)
            {
                // 正则编译失败不应阻塞请求，降级为仅长度校验
            }
        }
    }

    // 验证 password
    if (password.empty())
    {
        errors.push_back("password is required");
    }
    else if (password.length() > 200)
    {
        errors.push_back("password exceeds maximum length of 200 characters");
    }

    // 验证 email（必填字段：email 是主登录键）
    if (email.empty())
    {
        errors.push_back("email is required");
    }
    else if (email.length() > EMAIL_MAX_LEN)
    {
        errors.push_back(
          "email exceeds maximum length of " + std::to_string(EMAIL_MAX_LEN) + " characters"
        );
    }
    else
    {
        try
        {
            std::regex re(EMAIL_PATTERN);
            if (!std::regex_match(email, re))
            {
                errors.push_back("email format is invalid");
            }
        }
        catch (const std::regex_error &)
        {
            // 正则编译失败不应阻塞请求，降级为仅长度校验
        }
    }

    return errors;
}

// ========== P1: Token Introspection & Revocation Validation ==========

std::vector<std::string> RuleSet::oauth2Introspect(const ::drogon::HttpRequestPtr &req)
{
    std::vector<std::string> errors;

    // Extract token parameter (required)
    std::string token;
    if (req->method() == ::drogon::Post)
    {
        auto params = req->getParameters();
        token = params["token"];
    }
    else
    {
        token = req->getParameter("token");
    }

    // Validate token (required)
    auto tokenError = validateToken(token);
    if (tokenError.has_value())
    {
        errors.push_back(tokenError.value());
    }

    // token_type_hint is optional, no validation needed
    // Client authentication is handled separately in the controller

    return errors;
}

std::vector<std::string> RuleSet::oauth2Revoke(const ::drogon::HttpRequestPtr &req)
{
    std::vector<std::string> errors;

    // Extract token parameter (required)
    std::string token;
    if (req->method() == ::drogon::Post)
    {
        auto params = req->getParameters();
        token = params["token"];
    }
    else
    {
        token = req->getParameter("token");
    }

    // Validate token (required)
    auto tokenError = validateToken(token);
    if (tokenError.has_value())
    {
        errors.push_back(tokenError.value());
    }

    // token_type_hint is optional, no validation needed
    // Client authentication is handled separately in the controller

    return errors;
}

}  // namespace authforge::drogon::validation
