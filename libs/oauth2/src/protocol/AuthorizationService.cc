#include <authforge/oauth2/protocol/AuthorizationService.h>

#include <authforge/common/model/Subject.h>
#include <authforge/oauth2/access/ScopeDecisionEngine.h>
#include <authforge/oauth2/model/Client.h>

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace authforge::oauth2::protocol
{

namespace
{
authforge::oauth2::access::ScopeValidationSummary allInvalid(
  const std::vector<std::string> &scopes,
  const std::string &reason
)
{
    authforge::oauth2::access::ScopeValidationSummary summary;
    for (const auto &scope : scopes)
    {
        summary.invalid.push_back(scope);
        summary.invalidReasons.push_back(reason);
    }
    return summary;
}

bool anyRequiresAdminRole(const std::vector<std::string> &scopes)
{
    for (const auto &scope : scopes)
    {
        if (authforge::oauth2::access::isAdminScope(scope))
            return true;
    }
    return false;
}
}  // namespace

AuthorizationService::AuthorizationService(
  std::shared_ptr<authforge::oauth2::repository::IClientRepository> clients,
  std::shared_ptr<authforge::oauth2::repository::IConsentRepository> consents,
  std::shared_ptr<authforge::common::ports::ISubjectResolver> subjectResolver,
  std::shared_ptr<authforge::common::ports::IRoleProvider> roleProvider
)
    : clients_(std::move(clients)),
      consents_(std::move(consents)),
      subjectResolver_(std::move(subjectResolver)),
      roleProvider_(std::move(roleProvider))
{
}

void AuthorizationService::evaluateScopes(
  const std::string &clientId,
  const std::string &subject,
  const std::vector<std::string> &requestedScopes,
  std::function<void(authforge::oauth2::access::ScopeValidationSummary)> &&callback
)
{
    if (!clients_)
    {
        callback(allInvalid(requestedScopes, "server_error"));
        return;
    }

    clients_->getClient(
      clientId,
      [this, clientId, subject, requestedScopes, callback = std::move(callback)](
        std::optional<authforge::oauth2::model::OAuth2Client> clientOpt
      ) mutable {
          if (!clientOpt)
          {
              callback(allInvalid(requestedScopes, "client_not_found"));
              return;
          }

          auto client = std::make_shared<authforge::oauth2::model::Client>(std::move(*clientOpt));
          bool needsAdminCheck = anyRequiresAdminRole(requestedScopes);

          // Resolve internalUserId once (used for both the admin-role check
          // and the per-scope consent lookup below).
          auto resolveInternalUserId =
            [this](const std::string &subj, std::function<void(std::optional<int32_t>)> &&cb) {
                if (!subjectResolver_)
                {
                    cb(std::nullopt);
                    return;
                }
                authforge::common::model::Subject subjectValue(subj);
                subjectResolver_->resolve(subjectValue, std::move(cb));
            };

          resolveInternalUserId(
            subject,
            [this,
             client,
             clientId,
             subject,
             requestedScopes,
             needsAdminCheck,
             callback = std::move(callback)](std::optional<int32_t> internalUserId) mutable {
                auto proceedWithAdminFlag = [this,
                                             client,
                                             clientId,
                                             subject,
                                             requestedScopes,
                                             internalUserId,
                                             callback =
                                               std::move(callback)](bool hasAdminRole) mutable {
                    // Fan out consent lookups for every scope that is
                    // syntactically eligible (tiers 1-2 are re-checked
                    // inside evaluateScope itself; prefetching consent for
                    // every requested scope, even ones that will end up
                    // Invalid, is simpler than pre-filtering and is at most
                    // a few redundant lookups).
                    if (!consents_ || !internalUserId)
                    {
                        authforge::oauth2::access::ScopeValidationSummary summary =
                          authforge::oauth2::access::evaluateScopes(
                            requestedScopes, *client, hasAdminRole, [](const std::string &) {
                                return false;
                            }
                          );
                        callback(std::move(summary));
                        return;
                    }

                    auto consentMap = std::make_shared<std::unordered_map<std::string, bool>>();
                    auto remaining = std::make_shared<size_t>(requestedScopes.size());
                    if (requestedScopes.empty())
                    {
                        callback(
                          authforge::oauth2::access::evaluateScopes(
                            requestedScopes, *client, hasAdminRole, [](const std::string &) {
                                return false;
                            }
                          )
                        );
                        return;
                    }

                    authforge::oauth2::model::UserRef userRef{*internalUserId};
                    for (const auto &scope : requestedScopes)
                    {
                        consents_->hasUserConsent(
                          userRef,
                          clientId,
                          scope,
                          [consentMap,
                           remaining,
                           scope,
                           client,
                           hasAdminRole,
                           requestedScopes,
                           callback](bool hasConsent) mutable {
                              (*consentMap)[scope] = hasConsent;
                              if (--(*remaining) == 0)
                              {
                                  callback(
                                    authforge::oauth2::access::evaluateScopes(
                                      requestedScopes,
                                      *client,
                                      hasAdminRole,
                                      [consentMap](const std::string &s) {
                                          auto it = consentMap->find(s);
                                          return it != consentMap->end() && it->second;
                                      }
                                    )
                                  );
                              }
                          }
                        );
                    }
                };

                if (!needsAdminCheck || !roleProvider_ || !internalUserId)
                {
                    proceedWithAdminFlag(false);
                    return;
                }

                roleProvider_->getRoles(
                  *internalUserId,
                  [proceedWithAdminFlag =
                     std::move(proceedWithAdminFlag)](std::vector<std::string> roles) mutable {
                      bool hasAdmin = std::find(roles.begin(), roles.end(), "admin") != roles.end();
                      proceedWithAdminFlag(hasAdmin);
                  }
                );
            }
          );
      }
    );
}

}  // namespace authforge::oauth2::protocol
