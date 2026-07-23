#include <oauth2/validation/RuleEngine.h>
#include <regex>
#include <drogon/utils/Utilities.h>
#include <algorithm>

namespace authforge::drogon::validation
{

// Result static methods
Result Result::success()
{
    return Result{true, "", ""};
}

Result Result::failure(const std::string &field, const std::string &message)
{
    return Result{false, field, message};
}

// Basic validation methods
Result RuleEngine::notEmpty(const std::string &value, const std::string &fieldName)
{
    if (value.empty())
    {
        return Result::failure(fieldName, "Cannot be empty");
    }
    return Result::success();
}

Result RuleEngine::length(
  const std::string &value,
  const std::string &fieldName,
  size_t minLen,
  size_t maxLen
)
{
    if (value.length() < minLen)
    {
        return Result::failure(
          fieldName, "Must be at least " + std::to_string(minLen) + " characters"
        );
    }
    if (value.length() > maxLen)
    {
        return Result::failure(
          fieldName, "Must be at most " + std::to_string(maxLen) + " characters"
        );
    }
    return Result::success();
}

Result RuleEngine::regex(
  const std::string &value,
  const std::string &fieldName,
  const std::string &pattern
)
{
    try
    {
        std::regex re(pattern);
        if (!std::regex_match(value, re))
        {
            return Result::failure(fieldName, "Format is invalid");
        }
        return Result::success();
    }
    catch (const std::regex_error &)
    {
        return Result::failure(fieldName, "Invalid regex pattern");
    }
}

Result RuleEngine::numericRange(int value, const std::string &fieldName, int minVal, int maxVal)
{
    if (value < minVal || value > maxVal)
    {
        return Result::failure(
          fieldName, "Must be between " + std::to_string(minVal) + " and " + std::to_string(maxVal)
        );
    }
    return Result::success();
}

// OAuth2-specific validation
Result RuleEngine::validateClientId(const std::string &clientId)
{
    auto result1 = notEmpty(clientId, "client_id");
    if (!result1.ok)
        return result1;

    auto result2 = regex(clientId, "client_id", CLIENT_ID_PATTERN);
    if (!result2.ok)
    {
        return Result::failure("client_id", "Must be 1-128 alphanumeric characters (._- allowed)");
    }

    return length(clientId, "client_id", CLIENT_ID_MIN_LEN, CLIENT_ID_MAX_LEN);
}

Result RuleEngine::validateClientSecret(const std::string &secret)
{
    auto result1 = notEmpty(secret, "client_secret");
    if (!result1.ok)
        return result1;

    // Client secret: at least 12 characters, alphanumeric plus special chars
    auto result2 = regex(secret, "client_secret", "^[a-zA-Z0-9._~!@#$%^&*()-=+]{12,}$");
    if (!result2.ok)
    {
        return Result::failure(
          "client_secret",
          "Must be at least 12 alphanumeric "
          "characters (special chars allowed)"
        );
    }

    return Result::success();
}

Result RuleEngine::validateRedirectUri(const std::string &uri)
{
    auto result1 = notEmpty(uri, "redirect_uri");
    if (!result1.ok)
        return result1;

    auto result2 = regex(uri, "redirect_uri", REDIRECT_URI_PATTERN);
    if (!result2.ok)
    {
        return Result::failure("redirect_uri", "Must be a valid HTTP/HTTPS URL");
    }

    return length(uri, "redirect_uri", REDIRECT_URI_MIN_LEN, REDIRECT_URI_MAX_LEN);
}

Result RuleEngine::validateScope(const std::string &scope)
{
    auto result1 = notEmpty(scope, "scope");
    if (!result1.ok)
        return result1;

    auto result2 = regex(scope, "scope", SCOPE_PATTERN);
    if (!result2.ok)
    {
        return Result::failure(
          "scope", "Must contain only alphanumeric characters, colons, and spaces"
        );
    }

    return length(scope, "scope", SCOPE_MIN_LEN, SCOPE_MAX_LEN);
}

Result RuleEngine::validateResponseType(const std::string &type)
{
    auto result1 = notEmpty(type, "response_type");
    if (!result1.ok)
        return result1;

    auto result2 = regex(type, "response_type", RESPONSE_TYPE_PATTERN);
    if (!result2.ok)
    {
        return Result::failure("response_type", "Contains invalid characters");
    }

    // Check for valid OAuth2 response types
    if (type != "code" && type != "token")
    {
        return Result::failure("response_type", "Must be 'code' or 'token'");
    }

    return Result::success();
}

Result RuleEngine::validateGrantType(const std::string &type)
{
    auto result1 = notEmpty(type, "grant_type");
    if (!result1.ok)
        return result1;

    auto result2 = regex(type, "grant_type", GRANT_TYPE_PATTERN);
    if (!result2.ok)
    {
        return Result::failure("grant_type", "Contains invalid characters");
    }

    // Check for valid OAuth2 grant types
    const std::vector<std::string> validTypes =
      {"authorization_code", "client_credentials", "refresh_token", "password"};
    if (std::find(validTypes.begin(), validTypes.end(), type) == validTypes.end())
    {
        return Result::failure(
          "grant_type",
          "Must be one of: authorization_code, client_credentials, "
          "refresh_token, password"
        );
    }

    return Result::success();
}

Result RuleEngine::validateToken(const std::string &token)
{
    auto result1 = notEmpty(token, "token");
    if (!result1.ok)
        return result1;

    if (token.length() < TOKEN_MIN_LEN)
    {
        return Result::failure(
          "token", "Token must be at least " + std::to_string(TOKEN_MIN_LEN) + " characters"
        );
    }

    return regex(token, "token", TOKEN_PATTERN);
}

std::vector<Result> RuleEngine::validateAll(
  const std::vector<std::pair<std::string, std::string>> &fieldsAndValues,
  const std::vector<RuleType> &rules
)
{
    std::vector<Result> results;
    // Implementation for batch validation
    // This can be extended based on specific requirements
    return results;
}

}  // namespace authforge::drogon::validation
