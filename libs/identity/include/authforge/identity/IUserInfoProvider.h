#pragma once

#include <authforge/common/model/Subject.h>
#include <functional>
#include <optional>
#include <string>
#include <json/json.h>

namespace authforge::identity
{

// Import Subject into this namespace for convenience
using authforge::common::model::Subject;

/**
 * @brief Interface for retrieving user information
 * 
 * Provides OIDC-compliant user information (userinfo endpoint).
 */
class IUserInfoProvider
{
public:
    virtual ~IUserInfoProvider() = default;

    /**
     * @brief Get user information for a subject
     * @param subject The subject to get info for
     * @param scopes Granted scopes (determines which claims to include)
     * @param callback Async callback with user info JSON
     * 
     * Returns standard OIDC claims based on scopes:
     * - profile: name, family_name, given_name, middle_name, nickname, preferred_username,
     *           profile, picture, website, gender, birthdate, zoneinfo, locale, updated_at
     * - email: email, email_verified
     * - phone: phone_number, phone_number_verified
     * - address: address (JSON object)
     */
    virtual void getUserInfo(
      const Subject &subject,
      const std::vector<std::string> &scopes,
      std::function<void(std::optional<Json::Value>)> &&callback
    ) = 0;
};

}  // namespace authforge::identity
