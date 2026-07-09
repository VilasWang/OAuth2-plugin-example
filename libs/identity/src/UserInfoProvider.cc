#include <authforge/identity/IUserInfoProvider.h>

namespace authforge::identity
{

using authforge::common::model::Subject;

// Placeholder implementation - will be implemented in follow-up tasks
class UserInfoProvider : public IUserInfoProvider
{
public:
    void getUserInfo(
      const Subject &subject,
      const std::vector<std::string> &scopes,
      std::function<void(std::optional<Json::Value>)> &&callback
    ) override
    {
        // TODO: Implement - fetch user info from repository based on subject
        // TODO: Filter claims based on scopes (profile, email, phone, address)
        callback(std::nullopt);
    }
};

}  // namespace authforge::identity
