#pragma once

#include <drogon/drogon.h>
#include <string>
#include <utility>
#include <set>

namespace fulla::common::utils
{

/**
 * @brief Subject Generator for OAuth2/OpenID Connect
 *
 * This utility class handles the generation and parsing of OAuth2 subject
 * identifiers that include provider information to avoid conflicts across
 * different identity providers.
 *
 * Subject format: "provider:subject"
 * - local: "local:username"
 * - google: "google:google_sub"
 * - wechat: "wechat:openid"
 *
 * This design prevents subject conflicts when the same user ID exists across
 * different providers (e.g., user ID "12345" from both Google and WeChat).
 */
class SubjectGenerator
{
  public:
    // Default providers
    static constexpr const char *LOCAL = "local";
    static constexpr const char *GOOGLE = "google";
    static constexpr const char *WECHAT = "wechat";

    /**
     * @brief Generate subject for local login
     * @param username Local username
     * @return Subject in format "local:username"
     */
    static std::string forLocalUser(const std::string &username)
    {
        return std::string(LOCAL) + ":" + username;
    }

    /**
     * @brief Generate subject for Google login
     * @param googleSub Google's subject identifier (sub field from ID token)
     * @return Subject in format "google:google_sub"
     */
    static std::string forGoogleUser(const std::string &googleSub)
    {
        return std::string(GOOGLE) + ":" + googleSub;
    }

    /**
     * @brief Generate subject for WeChat login
     * @param openid WeChat OpenID
     * @return Subject in format "wechat:openid"
     */
    static std::string forWeChatUser(const std::string &openid)
    {
        return std::string(WECHAT) + ":" + openid;
    }

    /**
     * @brief Parse subject into provider and subject components
     * @param fullSubject Full subject string (e.g., "google:abc123")
     * @return Pair of (provider, subject)
     *
     * If no colon is found, returns ("local", fullSubject) as default.
     * If provider is not in the whitelist, logs a warning and returns ("local",
     * fullSubject).
     */
    static std::pair<std::string, std::string> parse(const std::string &fullSubject)
    {
        size_t colonPos = fullSubject.find(':');
        if (colonPos == std::string::npos)
        {
            // No colon found, assume local provider
            return {LOCAL, fullSubject};
        }

        std::string provider = fullSubject.substr(0, colonPos);
        std::string subject = fullSubject.substr(colonPos + 1);

        // Whitelist validation to prevent parsing errors with subjects
        // containing ":"
        static const std::set<std::string> VALID_PROVIDERS = {LOCAL, GOOGLE, WECHAT};

        if (VALID_PROVIDERS.find(provider) == VALID_PROVIDERS.end())
        {
            // Unknown provider, treat as local with original subject
            LOG_WARN << "Unknown provider in subject: " << fullSubject << ", treating as local";
            return {LOCAL, fullSubject};
        }

        return {provider, subject};
    }

    /**
     * @brief Generate subject for custom provider
     * @param provider Custom provider name
     * @param subject Subject identifier
     * @return Subject in format "provider:subject"
     */
    static std::string forProvider(const std::string &provider, const std::string &subject)
    {
        return provider + ":" + subject;
    }

    /**
     * @brief Validate subject format
     * @param subject Subject to validate
     * @return true if subject format is valid
     */
    static bool isValid(const std::string &subject)
    {
        if (subject.empty())
        {
            return false;
        }

        // Check if subject contains at least one colon
        size_t colonPos = subject.find(':');
        if (colonPos == std::string::npos || colonPos == 0 || colonPos == subject.length() - 1)
        {
            return false;
        }

        return true;
    }
};

}  // namespace fulla::common::utils
