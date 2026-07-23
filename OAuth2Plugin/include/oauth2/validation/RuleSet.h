#pragma once

#include <oauth2/validation/RuleEngine.h>
#include <oauth2/validation/Rules.h>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace authforge::drogon::validation
{

class RuleSet
{
  public:
    static std::optional<std::string> validateField(
      const std::string &value,
      const std::string &fieldName,
      const Rule &rule
    );
    static std::vector<std::string> validateFields(
      const std::vector<std::pair<std::string, std::string>> &fieldsAndValues,
      const std::vector<Rule> &rules
    );
    static std::vector<std::string> validateRequest(
      const ::drogon::HttpRequestPtr &req,
      const std::vector<Rule> &rules
    );

    static std::optional<std::string> validateClientId(const std::string &clientId);
    static std::optional<std::string> validateClientSecret(const std::string &secret);
    static std::optional<std::string> validateRedirectUri(const std::string &uri);
    static std::optional<std::string> validateScope(const std::string &scope);
    static std::optional<std::string> validateResponseType(const std::string &type);
    static std::optional<std::string> validateGrantType(const std::string &type);
    static std::optional<std::string> validateToken(const std::string &token);

    static std::vector<std::string> oauth2Authorize(const ::drogon::HttpRequestPtr &req);
    static std::vector<std::string> oauth2Token(const ::drogon::HttpRequestPtr &req);
    static std::vector<std::string> login(const ::drogon::HttpRequestPtr &req);
    static std::vector<std::string> registerUser(const ::drogon::HttpRequestPtr &req);
    static std::vector<std::string> oauth2Introspect(const ::drogon::HttpRequestPtr &req);
    static std::vector<std::string> oauth2Revoke(const ::drogon::HttpRequestPtr &req);

  private:
    static std::string extractFieldValue(
      const ::drogon::HttpRequestPtr &req,
      const std::string &field,
      const std::string &source
    );
    static std::string getValueFromSource(
      const ::drogon::HttpRequestPtr &req,
      const std::string &field,
      const std::string &source
    );
};

}  // namespace authforge::drogon::validation
