#include <authforge/drogon/services/IdentityService.h>
#include <authforge/drogon/utils/SubjectGenerator.h>
#include <drogon/drogon.h>

namespace
{
// Wrap an int32 internalUserId into the NEW consent repo's UserRef (F4:
// consent no longer takes a bare int32). Per UserRef.h's documented contract
// this boundary (a storage/service consumer) is permitted to set internalUserId
// directly.
authforge::oauth2::model::UserRef toUserRef(int32_t internalUserId)
{
    authforge::oauth2::model::UserRef u;
    u.internalUserId = internalUserId;
    return u;
}
}  // namespace

namespace authforge::identity
{

IdentityService::IdentityService(Repos repos) : repos_(std::move(repos))
{
}

void IdentityService::getUserRoles(
  const std::string &userId,
  std::function<void(std::vector<std::string>)> &&callback
)
{
    if (!repos_.role)
    {
        callback({});
        return;
    }
    repos_.role->getRoles(userId, std::move(callback));
}

void IdentityService::ensureSubjectMapping(
  const std::string &subject,
  const std::string & /*username*/,
  int32_t internalUserId,
  std::function<void()> &&callback
)
{
    if (!repos_.subjectMapping)
    {
        callback();
        return;
    }

    auto [provider, sub] = authforge::common::utils::SubjectGenerator::parse(subject);

    // Defect 1.9 fix: capture `self` (shared owner) at the OUTERMOST async call
    // and thread the SAME `self` through the nested continuation, so the
    // service stays alive until the in-flight callback completes.
    auto self = shared_from_this();
    repos_.subjectMapping->getInternalUserId(
      sub,
      provider,
      [self, this, sub, provider, internalUserId, callback = std::move(callback)](
        auto existingUserId
      ) {
          if (existingUserId)
          {
              callback();
              return;
          }

          repos_.subjectMapping->createSubjectMapping(
            sub, internalUserId, provider, [callback = std::move(callback)](bool /*success*/) {
                callback();
            }
          );
      }
    );
}

void IdentityService::handleFirstTimeLogin(
  const std::string &subject,
  const std::string & /*providerArg*/,
  std::function<void(int32_t)> &&callback
)
{
    if (!repos_.subjectMapping)
    {
        callback(0);
        return;
    }

    auto [prov, sub] = authforge::common::utils::SubjectGenerator::parse(subject);

    // Create a real user in the database via the subject-mapping repo
    // (createUserForExternalLogin lives on ISubjectMappingRepository, carved
    // from the god interface alongside createSubjectMapping).
    //
    // Defect 1.9 fix: capture `self` (shared owner) at the OUTERMOST async call
    // and thread the SAME `self` through the nested continuation, so the
    // service stays alive until the in-flight callback completes.
    auto self = shared_from_this();
    repos_.subjectMapping->createUserForExternalLogin(
      sub,
      prov,
      [self, this, sub, prov, callback = std::move(callback)](std::optional<int32_t> newUserId) {
          if (!newUserId || *newUserId == 0)
          {
              LOG_ERROR << "Failed to create user for external login: " << prov << ":" << sub;
              callback(0);
              return;
          }
          // Create subject mapping
          repos_.subjectMapping->createSubjectMapping(
            sub,
            *newUserId,
            prov,
            [newUserId = *newUserId, callback = std::move(callback)](bool success) {
                callback(success ? newUserId : 0);
            }
          );
      }
    );
}

void IdentityService::getInternalUserId(
  const std::string &subject,
  std::function<void(std::optional<int32_t>)> &&callback
)
{
    if (!repos_.subjectMapping)
    {
        callback(std::nullopt);
        return;
    }

    auto [provider, sub] = authforge::common::utils::SubjectGenerator::parse(subject);

    repos_.subjectMapping->getInternalUserId(sub, provider, std::move(callback));
}

void IdentityService::hasUserConsent(
  int32_t internalUserId,
  const std::string &clientId,
  const std::string &scope,
  std::function<void(bool)> &&callback
)
{
    if (!repos_.consent)
    {
        callback(false);
        return;
    }
    repos_.consent->hasUserConsent(toUserRef(internalUserId), clientId, scope, std::move(callback));
}

void IdentityService::saveUserConsent(
  int32_t internalUserId,
  const std::string &clientId,
  const std::string &scope,
  std::function<void(bool)> &&callback
)
{
    if (!repos_.consent)
    {
        callback(false);
        return;
    }
    repos_.consent
      ->saveUserConsent(toUserRef(internalUserId), clientId, scope, std::move(callback));
}

void IdentityService::validateUserRolesForScopes(
  const std::string &userId,
  const std::vector<std::string> &scopes,
  std::function<void(bool, std::string)> &&callback
)
{
    if (!repos_.role)
    {
        callback(false, "Storage not initialized");
        return;
    }

    std::vector<std::string> adminScopes;
    for (const auto &scope : scopes)
    {
        if (scopeRequiresAdminRole(scope))
        {
            adminScopes.push_back(scope);
        }
    }

    if (adminScopes.empty())
    {
        callback(true, "");
        return;
    }

    getUserRoles(
      userId,
      [callback = std::move(callback), adminScopes](std::vector<std::string> userRoles) mutable {
          bool hasAdminRole = false;
          for (const auto &role : userRoles)
          {
              if (role == "admin")
              {
                  hasAdminRole = true;
                  break;
              }
          }

          if (!hasAdminRole)
          {
              callback(false, "Admin role required for requested scopes");
              return;
          }

          callback(true, "");
      }
    );
}

bool IdentityService::scopeRequiresAdminRole(const std::string &scope)
{
    static const std::vector<std::string> adminScopes =
      {"admin", "admin:read", "admin:write", "user:manage", "settings:manage"};

    for (const auto &adminScope : adminScopes)
    {
        if (scope == adminScope || scope.find(adminScope + ":") == 0)
        {
            return true;
        }
    }
    return false;
}

}  // namespace authforge::identity
