#include <authforge/identity/AuthService.h>
#include <authforge/identity/IUserRepository.h>
// TODO: Remove dependency on oauth2 utils (Task 14 - extract crypto/hash to common ports)
// #include <oauth2/utils/PasswordHasher.h>
// #include <oauth2/utils/EmailNormalizer.h>

namespace authforge::identity
{

AuthService::AuthService(
  std::shared_ptr<IUserRepository> userRepo,
  std::shared_ptr<ISubjectResolver> subjectResolver,
  std::shared_ptr<IUserInfoProvider> userInfoProvider
)
  : userRepo_(std::move(userRepo))
  , subjectResolver_(std::move(subjectResolver))
  , userInfoProvider_(std::move(userInfoProvider))
{
}

void AuthService::validateUser(
  const std::string &identifier,
  const std::string &password,
  std::function<void(std::optional<AuthResult>)> &&callback
)
{
    // TODO: Implement once crypto ports are available
    // Login identifier routing: contains @ treated as email (normalize then query), otherwise query by username
    // bool isEmail = identifier.find('@') != std::string::npos;
    // std::string lookupKey = isEmail ? normalizeEmail(identifier) : identifier;
    
    callback(std::nullopt);  // Placeholder
}

void AuthService::registerUser(
  const std::string &username,
  const std::string &password,
  const std::string &email,
  std::function<void(const std::string &)> &&callback
)
{
    // TODO: Implement registration logic
    // - Validate username/email format
    // - Check for existing user
    // - Hash password
    // - Create user record
    // - Generate public sub (UUID)
    callback("NOT_IMPLEMENTED");
}

void AuthService::getUserInfo(
  int64_t userId,
  const std::vector<std::string> &scopes,
  std::function<void(std::optional<Json::Value>)> &&callback
)
{
    // Delegate to userInfoProvider
    // TODO: Map internal userId to Subject
    callback(std::nullopt);
}

}  // namespace authforge::identity
