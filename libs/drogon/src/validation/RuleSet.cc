#include <fulla/drogon/validation/RuleSet.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <cctype>
#include <regex>
#include <algorithm>

namespace fulla::drogon::validation
{

namespace
{

// #57: host-literal check backing validateBackchannelLogoutUri. Covers the
// literal forms an admin can type -- "localhost", dotted-quad IPv4, and
// bracketed/leading IPv6 -- against loopback/private/link-local ranges.
// Deliberately does NOT do DNS resolution (a name resolving to a private IP
// at validation time can rebind later anyway); this closes the
// copy-paste-a-metadata-URL class, and delivery-time hardening lives in the
// HTTP adapter.
bool isPrivateIpv4(const std::string &host)
{
    // Strict dotted-quad parse (no sscanf: MSVC deprecates it, and trailing
    // garbage must not classify). Each octet: 1-3 digits, value <= 255.
    unsigned int octets[4] = {0, 0, 0, 0};
    size_t idx = 0;
    std::string token;
    auto classify = [&octets]() {
        if (octets[0] == 0 || octets[0] == 10 || octets[0] == 127)
            return true;  // 0/8, 10/8, 127/8
        if (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31)
            return true;  // 172.16/12
        if (octets[0] == 192 && octets[1] == 168)
            return true;  // 192.168/16
        if (octets[0] == 169 && octets[1] == 254)
            return true;  // 169.254/16 (link-local + cloud metadata)
        return false;
    };
    for (char ch : host)
    {
        if (ch == '.')
        {
            if (token.empty() || token.size() > 3 || idx >= 4)
                return false;
            octets[idx++] = static_cast<unsigned int>(std::stoul(token));
            token.clear();
        }
        else if (ch >= '0' && ch <= '9')
        {
            token.push_back(ch);
        }
        else
        {
            return false;  // non-numeric char: not a dotted quad
        }
    }
    if (token.empty() || token.size() > 3 || idx != 3)
        return false;
    octets[3] = static_cast<unsigned int>(std::stoul(token));
    for (unsigned int o : octets)
        if (o > 255)
            return false;
    return classify();
}

bool isPrivateIpv6(const std::string &host)
{
    // host arrives bracket-stripped (e.g. "::1", "fe80::1", "fd00::1").
    if (host == "::1" || host == "::")
        return true;  // loopback + unspecified
    if (host.rfind("::ffff:", 0) == 0)
        return isPrivateIpv4(host.substr(7));  // v4-mapped
    std::string h;
    h.reserve(host.size());
    for (char ch : host)
        h.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(ch))));
    // Skip a leading "::" so "::fd12::1"-style forms still classify by the
    // first hextet that follows.
    const std::string leading = h.rfind("::", 0) == 0 ? h.substr(2) : h;
    if (leading.rfind("fc", 0) == 0 || leading.rfind("fd", 0) == 0)
        return true;  // fc00::/7 unique-local
    if (leading.rfind("fe8", 0) == 0 || leading.rfind("fe9", 0) == 0 ||
        leading.rfind("fea", 0) == 0 || leading.rfind("feb", 0) == 0)
        return true;  // fe80::/10 link-local
    return false;
}

bool isPrivateOrLoopbackHost(const std::string &host)
{
    std::string h;
    h.reserve(host.size());
    for (char ch : host)
        h.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(ch))));
    if (h == "localhost" || h == "localhost.")
        return true;
    if (h.find(':') != std::string::npos)
        return isPrivateIpv6(h);
    return isPrivateIpv4(h);
}

}  // namespace

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
    // #57 hardening: the OP POSTs a signed logout_token to this URI
    // server-side on every logout, so scheme-prefix checking alone is an
    // SSRF-shaped hole. Beyond the https requirement (OIDC Back-Channel
    // Logout 1.0 §2.3) this now enforces URL structure (non-empty host, no
    // userinfo, no fragment, length <= the VARCHAR(512) column) and rejects
    // loopback/private/link-local host literals unless the dedicated
    // auth.allow_private_backchannel_logout_uri opt-in (NOT the
    // allow_http_redirect_uri browser-redirect hatch) permits them.

    // Empty == "not configured": valid (the notifier skips clients without a
    // backchannel_logout_uri).
    if (uri.empty())
        return std::nullopt;

    if (uri.size() > 512)
        return std::string{"backchannel_logout_uri exceeds 512 characters"};

    const bool https = uri.rfind("https://", 0) == 0;
    const bool http = uri.rfind("http://", 0) == 0;
    if (!https && !http)
        return std::string{"backchannel_logout_uri must use https"};
    if (http)
    {
        // Dev hatch parity with redirect_uri validation (loopback literals
        // are NOT exempt here: server-to-server delivery).
        const auto &cfg = ::drogon::app().getCustomConfig();
        const bool allowHttp =
          cfg.isMember("auth") && cfg["auth"].isMember("allow_http_redirect_uri") &&
          cfg["auth"]["allow_http_redirect_uri"].asBool();
        if (!allowHttp)
            return std::string{"backchannel_logout_uri must use https"};
    }

    // Structure: scheme://authority/path?query -- no fragment allowed.
    if (uri.find('#') != std::string::npos)
        return std::string{"backchannel_logout_uri must not contain a fragment"};

    const std::string rest = uri.substr(uri.find("://") + 3);
    const size_t authEnd = rest.find_first_of("/?");
    const std::string authority =
      (authEnd == std::string::npos) ? rest : rest.substr(0, authEnd);

    // Userinfo (user:pass@host) has no meaning for a logout callback and
    // hides the real host from casual review.
    if (authority.find('@') != std::string::npos)
        return std::string{"backchannel_logout_uri must not contain userinfo"};

    // Host: IPv6 [v6]:port bracket form or host[:port].
    std::string host;
    if (!authority.empty() && authority[0] == '[')
    {
        const size_t close = authority.find(']');
        if (close == std::string::npos)
            return std::string{"backchannel_logout_uri has a malformed IPv6 host"};
        host = authority.substr(1, close - 1);
    }
    else
    {
        const size_t colon = authority.find(':');
        host = (colon == std::string::npos) ? authority : authority.substr(0, colon);
    }
    if (host.empty())
        return std::string{"backchannel_logout_uri must include a host"};

    if (isPrivateOrLoopbackHost(host))
    {
        const auto &cfg = ::drogon::app().getCustomConfig();
        const bool allowPrivate = cfg.isMember("auth") &&
                                  cfg["auth"].isMember("allow_private_backchannel_logout_uri") &&
                                  cfg["auth"]["allow_private_backchannel_logout_uri"].asBool();
        if (!allowPrivate)
            return std::string{
              "backchannel_logout_uri must not target loopback/private/link-local "
              "addresses (set auth.allow_private_backchannel_logout_uri to permit "
              "them for local testing)"
            };
    }

    return std::nullopt;
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
    else if (password.length() < passwordMinLength())
    {
        errors.push_back("password must be at least " + std::to_string(passwordMinLength()) + " characters");
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

}  // namespace fulla::drogon::validation

namespace fulla::drogon::validation
{
size_t RuleSet::passwordMinLength_ = 8;
}  // namespace fulla::drogon::validation
