#pragma once

#include <fulla/drogon/validation/RuleEngine.h>
#include <fulla/drogon/validation/Rule.h>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

namespace fulla::drogon::validation
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
    /// Validate an OIDC Back-Channel Logout 1.0 backchannel_logout_uri (§2.3:
    /// MUST use https; empty is valid == "not configured"). Honors the
    /// auth.allow_http_redirect_uri dev hatch for parity; loopback is NOT
    /// exempt (server-to-server delivery). Returns an error message or nullopt.
    static std::optional<std::string> validateBackchannelLogoutUri(const std::string &uri);
    static std::optional<std::string> validateScope(const std::string &scope);
    static std::optional<std::string> validateResponseType(const std::string &type);
    static std::optional<std::string> validateGrantType(const std::string &type);
    static std::optional<std::string> validateToken(const std::string &token);

    static std::vector<std::string> oauth2Authorize(const ::drogon::HttpRequestPtr &req);
    static std::vector<std::string> oauth2Token(const ::drogon::HttpRequestPtr &req);
    static std::vector<std::string> login(const ::drogon::HttpRequestPtr &req);
    // D4 (#103): minimum password length policy (auth.min_password_length,
    // default 8). Static because RuleSet is a stateless validator; the server
    // assembly (and tests) set it once at startup.
    static void setPasswordMinLength(size_t minLen) { passwordMinLength_ = minLen; }
    static size_t passwordMinLength() { return passwordMinLength_; }

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
  private:
    static size_t passwordMinLength_;
};

}  // namespace fulla::drogon::validation
