#pragma once

// M3 Task 20: relocated verbatim from
// OAuth2Plugin/include/oauth2/validation/RuleEngine.h into
// authforge::drogon::validation (see Rules.h in this directory for the
// migration rationale).

#include <authforge/drogon/validation/Rules.h>
#include <vector>
#include <utility>

namespace authforge::drogon::validation
{

class RuleEngine
{
  public:
    static Result notEmpty(const std::string &value, const std::string &fieldName);
    static Result length(
      const std::string &value,
      const std::string &fieldName,
      size_t minLen,
      size_t maxLen
    );
    static Result regex(
      const std::string &value,
      const std::string &fieldName,
      const std::string &pattern
    );
    static Result numericRange(int value, const std::string &fieldName, int minVal, int maxVal);

    static Result validateClientId(const std::string &clientId);
    static Result validateClientSecret(const std::string &secret);
    static Result validateRedirectUri(const std::string &uri);
    static Result validateScope(const std::string &scope);
    static Result validateResponseType(const std::string &type);
    static Result validateGrantType(const std::string &type);
    static Result validateToken(const std::string &token);

    static std::vector<Result> validateAll(
      const std::vector<std::pair<std::string, std::string>> &fieldsAndValues,
      const std::vector<RuleType> &rules
    );
};

}  // namespace authforge::drogon::validation
