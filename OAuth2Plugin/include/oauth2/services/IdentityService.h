#pragma once

#include <oauth2/storage/IRoleRepository.h>
#include <oauth2/storage/IUserRepository.h>
#include <oauth2/storage/ISubjectMappingRepository.h>
#include <authforge/oauth2/repository/IConsentRepository.h>
#include <authforge/oauth2/model/UserRef.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace authforge::identity
{

// Defect 1.9 fix (async-chain dangling `this`): IdentityService inherits
// std::enable_shared_from_this so its asynchronous storage chains
// (ensureSubjectMapping / handleFirstTimeLogin / validateUserRolesForScopes)
// can capture `auto self = shared_from_this();` at the outermost async call and
// thread that same `self` through every nested continuation, keeping the
// service alive until the in-flight callback completes (no use-after-free on
// teardown). The service is always created via std::make_shared
// (OAuth2Plugin::initAndStart), so shared_from_this() is valid at runtime. The
// synchronous pure-function call site (scopeRequiresAdminRole via a
// stack-constructed IdentityService({}) temporary) never calls
// shared_from_this(), so it keeps working without shared ownership.
//
// Phase 4.6a (authforge-sdk-refactor): this service is no longer keyed on the
// god IOAuth2Storage facade. It now holds the four identity-side split
// repositories (role / user / subject-mapping / consent) extracted from the
// per-backend RepositoryBundle. The consent repo is the NEW
// authforge::oauth2::repository::IConsentRepository (UserRef-based); the int32
// internalUserId exposed by this service's API is wrapped into a UserRef at the
// boundary. The other three repos are still on the legacy oauth2::* interfaces
// (identity-side migration to authforge::identity::* is a separate follow-up).
class IdentityService : public std::enable_shared_from_this<IdentityService>
{
  public:
    struct Repos
    {
        std::shared_ptr<::oauth2::IRoleRepository> role;
        std::shared_ptr<::oauth2::IUserRepository> user;
        std::shared_ptr<::oauth2::ISubjectMappingRepository> subjectMapping;
        std::shared_ptr<authforge::oauth2::repository::IConsentRepository> consent;
    };

    // A default-constructed Repos (all empty) is accepted for the pure-function
    // call sites (e.g. OAuth2Plugin::scopeRequiresAdminRole via
    // IdentityService({})).
    explicit IdentityService(Repos repos);

    void getUserRoles(
      const std::string &userId,
      std::function<void(std::vector<std::string>)> &&callback
    );

    void ensureSubjectMapping(
      const std::string &subject,
      const std::string &username,
      int32_t internalUserId,
      std::function<void()> &&callback
    );

    void handleFirstTimeLogin(
      const std::string &subject,
      const std::string &provider,
      std::function<void(int32_t)> &&callback
    );

    void getInternalUserId(
      const std::string &subject,
      std::function<void(std::optional<int32_t>)> &&callback
    );

    void hasUserConsent(
      int32_t internalUserId,
      const std::string &clientId,
      const std::string &scope,
      std::function<void(bool)> &&callback
    );

    void saveUserConsent(
      int32_t internalUserId,
      const std::string &clientId,
      const std::string &scope,
      std::function<void(bool)> &&callback
    );

    void validateUserRolesForScopes(
      const std::string &userId,
      const std::vector<std::string> &scopes,
      std::function<void(bool, std::string)> &&callback
    );

    bool scopeRequiresAdminRole(const std::string &scope);

  private:
    Repos repos_;
};

}  // namespace authforge::identity
