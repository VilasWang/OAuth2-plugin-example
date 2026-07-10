#pragma once

// Task 17 remainder (authforge-sdk-refactor, design.md §6/§8 "protocol/"):
// AuthorizationService did not exist anywhere in the pre-migration
// codebase as a distinct class -- the consent/scope decision orchestration
// design.md calls for was spread across ClientService::validateClientScopes
// (Tier 1: client allowlist), IdentityService::validateUserRolesForScopes
// + scopeRequiresAdminRole (Tier 2: admin role), and
// OAuth2StandardController::checkUserConsentAndProceed's own recursive
// plugin->hasUserConsent(...) walk (Tier 3: per-scope consent) -- three
// separate call sites in two services plus a controller, with no single
// place owning the combined decision.
//
// This class is that single place: it drives
// authforge::oauth2::access::evaluateScopes (the pure decision engine,
// Task 17 slice 7) by resolving the facts that engine needs (the Client
// aggregate via IClientRepository, the admin-role flag via
// ISubjectResolver+IRoleProvider, and per-scope consent via
// IConsentRepository) and returns a single ScopeValidationSummary the
// caller can act on (proceed / redirect to consent / reject) instead of
// threading three separate async calls together itself.
//
// NOT YET WIRED INTO PRODUCTION: OAuth2StandardController's consent flow
// (OAuth2Plugin/libs/drogon) continues to use its own inline
// plugin->validateClientScopes/validateUserRolesForScopes/hasUserConsent
// call chain. Wiring this class in is Task 24 (apps/server assembly),
// deferred until libs/identity's remaining services are filled in (see
// PROGRESS.md). This class is additive and independently unit-tested.

#include <authforge/common/ports/IRoleProvider.h>
#include <authforge/common/ports/ISubjectResolver.h>
#include <authforge/oauth2/access/ScopeDecision.h>
#include <authforge/oauth2/repository/IClientRepository.h>
#include <authforge/oauth2/repository/IConsentRepository.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace authforge::oauth2::protocol
{

class AuthorizationService
{
  public:
    AuthorizationService(
      std::shared_ptr<authforge::oauth2::repository::IClientRepository> clients,
      std::shared_ptr<authforge::oauth2::repository::IConsentRepository> consents = nullptr,
      std::shared_ptr<authforge::common::ports::ISubjectResolver> subjectResolver = nullptr,
      std::shared_ptr<authforge::common::ports::IRoleProvider> roleProvider = nullptr
    );

    /**
     * @brief Evaluate every scope in `requestedScopes` for `clientId` +
     * `subject` (client allowlist -> admin role -> user consent, in that
     * tier order -- matches the pre-existing production sequence). The
     * consent tier's `internalUserId` (needed by IConsentRepository, which
     * is keyed by UserRef) is resolved via subjectResolver_ internally;
     * if subjectResolver_ is unset, or the subject does not resolve, or
     * consents_ is unset, every scope that passes tiers 1-2 is reported
     * as ConsentRequired (the safe default: cannot confirm consent, so do
     * not assume it).
     *
     * @param clientId Client identifier.
     * @param subject OAuth2 subject (e.g. "local:alice").
     * @param requestedScopes The scopes being requested.
     * @param callback Invoked with the aggregated decision. If `clientId`
     * does not resolve to a known client, every scope is reported Invalid
     * with reason "client_not_found".
     */
    void evaluateScopes(
      const std::string &clientId,
      const std::string &subject,
      const std::vector<std::string> &requestedScopes,
      std::function<void(authforge::oauth2::access::ScopeValidationSummary)> &&callback
    );

  private:
    std::shared_ptr<authforge::oauth2::repository::IClientRepository> clients_;
    std::shared_ptr<authforge::oauth2::repository::IConsentRepository> consents_;
    std::shared_ptr<authforge::common::ports::ISubjectResolver> subjectResolver_;
    std::shared_ptr<authforge::common::ports::IRoleProvider> roleProvider_;
};

}  // namespace authforge::oauth2::protocol
