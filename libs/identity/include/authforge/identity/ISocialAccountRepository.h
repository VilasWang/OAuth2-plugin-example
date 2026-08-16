#pragma once

#ifdef WITH_SOCIAL

// M2.5 identity completion, Social auth slice (authforge-sdk-refactor,
// design.md §5.1/§6): repository interface backing GitHubAuthService's
// local-account linking flow -- the only one of the three social
// providers whose controller
// (libs/drogon/src/controllers/GitHubController.cc's login()) does more
// than "exchange code, fetch profile, hand it back to the caller": it
// also finds-or-creates a local `users` row and an
// `oauth2_subject_mappings` row, and assigns the default 'user' role, all
// via raw SQL.
//
// Why a new interface instead of extending ISubjectMappingRepository or
// IUserRepository (this header's own design decision, as called out by
// the task):
//   - authforge::identity::ISubjectMappingRepository (this package) is
//     deliberately READ-ONLY today (see that header's own "Scope note"
//     comment) and already has two implementors outside this task's
//     control -- libs/storage-postgres's PostgresIdentityRepository and
//     libs/identity/test/SubjectResolverTest.cc's FakeSubjectMappingRepository.
//     Adding pure-virtual write methods to it would be a breaking change
//     to both, which is out of scope for this ADDITIVE slice; adding
//     them with default implementations would blur
//     ISubjectMappingRepository's own documented single responsibility
//     (subject -> internal-user-id resolution) with a create-user
//     side-effect that has nothing to do with ISubjectResolver, the port
//     it exists to back.
//   - IUserRepository::create() already owns "create a user row + default
//     role assignment" for the local-registration path
//     (AuthService.cc's registerUser -- see that header's own comment:
//     "IUserRepository::create() is responsible for default-role
//     assignment"). GitHub's flow is a different shape (find existing
//     mapping OR create user+mapping+role as one linked operation,
//     keyed by provider+subject, not by email/username), so reusing
//     IUserRepository::create() directly would require the caller
//     (GitHubAuthService) to orchestrate the mapping/role steps itself
//     across two other repositories -- exactly the kind of raw-SQL,
//     multi-table orchestration this migration exists to move out of
//     framework-independent business logic and into a repository.
//   - Mirrors IMfaRepository.h/IWebAuthnRepository.h's own precedent:
//     "one repository per bounded concern", not growing an existing
//     interface or a god interface. The bounded concern here is
//     "find-or-create the local account behind a social login".
//
// Repository-owned default role assignment: like
// AuthService.h/registerUser's existing contract, createLinkedUser() is
// responsible for assigning the default 'user' role as part of the same
// create flow (mirrors the controller's own best-effort, non-blocking
// role-assignment step -- see GitHubAuthService.cc's comment on why a
// role-assignment failure does not fail account creation).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace authforge::identity
{

/**
 * @brief Outcome of an existing-provider-account lookup
 * (ISocialAccountRepository::findLinkedUser). Explicitly distinguishes the
 * four states the old optional<SocialAccountLookup> conflated (#54): a DB
 * error used to be indistinguishable from "no mapping yet" (the service then
 * wrongly attempted account creation), and "mapping exists but the linked
 * user is soft-deleted/locked" was not expressible at all — deleted users
 * received fresh tokens (V024 soft-delete contract bypass).
 */
enum class SocialLinkStatus
{
    Linked,             ///< Mapping exists and the user is live: issue tokens.
    NoMapping,          ///< No (provider, subject) mapping yet: create account.
    AccountUnavailable, ///< Mapping exists but the user is soft-deleted or
                        ///< locked: REJECT the login (generic auth error, no
                        ///< account-status enumeration).
    RepositoryError     ///< DB failure: reject with a DB error (do NOT fall
                        ///< through to account creation).
};

/**
 * @brief Result of an existing provider-account lookup
 * (ISocialAccountRepository::findLinkedUser).
 */
struct SocialAccountLookup
{
    int32_t userId = 0;  // Internal user id already linked to this provider+subject.
    std::string username;
};

/**
 * @brief Result of newly linking a provider account to a freshly created
 * local user (ISocialAccountRepository::createLinkedUser).
 */
struct LinkNewSocialAccountResult
{
    int32_t userId = 0;
    std::string username;
};

/**
 * @brief Repository for the "local account behind a social login" bounded
 * concern -- provider+subject -> internal user, and the linked-user
 * creation flow when no such mapping exists yet.
 */
class ISocialAccountRepository
{
  public:
    virtual ~ISocialAccountRepository() = default;

    /**
     * @brief Callback for findLinkedUser: the lookup status plus (for
     * SocialLinkStatus::Linked) the linked user's id + username. The lookup
     * payload is default/empty for every non-Linked status.
     */
    using LookupCallback =
      std::function<void(SocialLinkStatus status, const SocialAccountLookup &lookup)>;
    using CreateCallback = std::function<void(std::optional<LinkNewSocialAccountResult>)>;

    /**
     * @brief Look up the local user already linked to a
     * (provider, subject) pair, with soft-delete/lock enforcement (#54, V024
     * contract: a deleted user "can no longer log in").
     *
     * Contract per status:
     *   - Linked: lookup.userId/username populated.
     *   - NoMapping: no mapping row exists; caller may create the account.
     *   - AccountUnavailable: mapping exists but the users row is missing,
     *     soft-deleted (deleted_at NOT NULL), or locked (locked_until > now)
     *     — the caller MUST reject the login.
     *   - RepositoryError: a DB failure occurred at any step.
     *
     * @param provider Provider name (e.g. "github").
     * @param subject Provider-scoped subject identifier (e.g. the
     * GitHub numeric user id, stringified).
     * @param cb Status callback (see LookupCallback).
     */
    virtual void findLinkedUser(
      const std::string &provider,
      const std::string &subject,
      LookupCallback &&cb
    ) = 0;

    /**
     * @brief Create a new local user, link it to (provider, subject), and
     * assign the default 'user' role -- all as one repository-owned
     * operation (GitHubController.cc's INSERT users ON CONFLICT ... /
     * INSERT oauth2_subject_mappings / INSERT user_roles sequence).
     * @param provider Provider name (e.g. "github").
     * @param subject Provider-scoped subject identifier.
     * @param username Local username to create (caller is responsible
     * for provider-specific derivation, e.g. "gh_" + login).
     * @param email Email address to store on the new user record (may be
     * empty if the provider did not return one).
     * @param cb Callback with the new user's id + username on success,
     * or nullopt on failure (any repository/DB error). Role-assignment
     * failure is NOT a reason to fail this call (mirrors the
     * controller's own best-effort semantics).
     */
    virtual void createLinkedUser(
      const std::string &provider,
      const std::string &subject,
      const std::string &username,
      const std::string &email,
      CreateCallback &&cb
    ) = 0;
};

}  // namespace authforge::identity

#endif  // WITH_SOCIAL
