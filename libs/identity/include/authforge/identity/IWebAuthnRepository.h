#pragma once

// M2.5 identity completion (authforge-sdk-refactor, design.md §5.1/§6):
// repository interface backing WebAuthnService, mirroring the raw SQL
// libs/drogon/src/controllers/WebAuthnController.cc issues directly
// against the `webauthn_credentials` table (columns: user_id,
// credential_id, public_key, name, sign_count, created_at, last_used_at),
// joined with `users.public_sub` in authenticateFinish's lookup query.
// Keeping this as its own narrow interface (rather than growing
// IUserRepository) mirrors IMfaRepository's existing precedent: one
// repository per bounded set of columns/queries, not one god interface.
//
// Scope boundary (design.md §4.1 rule 2, identity <-> oauth2 互不依赖):
// this interface -- and WebAuthnService built on top of it -- only models
// the identity-owned state (credential rows keyed by an internal user
// id). It does not know about OAuth2 clients/tokens/sessions; the
// existing production controller's session-based challenge storage and
// its "then issue tokens" follow-up are Adapter/orchestration concerns
// that stay outside this package, same rationale as MfaService.h's own
// scope-boundary comment on the identity/oauth2 split.
//
// Internal user id vs public_sub: like IMfaRepository/IUserRepository,
// every method here is keyed by the internal auto-increment user id
// (int64_t), not the public_sub string the current controller happens to
// receive in its "userId" request attribute. Resolving public_sub ->
// internal id is the caller's responsibility (e.g. via
// authforge::common::ports::ISubjectResolver, same as AuthService/
// MfaService's existing convention) -- this repository does not do that
// resolution itself.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace authforge::identity
{

/**
 * @brief Outcome of IWebAuthnRepository::storeCredential.
 *
 * Mirrors the two DB-level outcomes WebAuthnController.cc's
 * registerFinish distinguishes today (via string-matching the
 * DrogonDbException message for the unique constraint on
 * webauthn_credentials.credential_id) -- surfaced here as a proper
 * repository-level result instead of exception text sniffing, since the
 * Domain layer must not depend on DrogonDbException (design.md §4.1
 * rule 1).
 */
enum class StoreCredentialOutcome
{
    Success,
    DuplicateCredentialId,  // credential_id already registered (unique constraint).
    Error                    // Any other repository failure (unknown user, DB error, etc).
};

/**
 * @brief Result of IWebAuthnRepository::findByCredentialId -- the columns
 * the controller's authenticateFinish query joins across
 * webauthn_credentials and users (wc.user_id, wc.sign_count,
 * u.public_sub).
 */
struct WebAuthnCredentialLookup
{
    int64_t userId = 0;       // Internal user id (webauthn_credentials.user_id).
    std::string publicSub;    // users.public_sub -- what the caller reports back as "user_id".
    int signCount = 0;        // webauthn_credentials.sign_count, as currently stored.
};

/**
 * @brief One row of IWebAuthnRepository::listCredentials -- the columns
 * the controller's listCredentials query selects (credential_id, name,
 * sign_count, created_at, last_used_at). Note the controller's current
 * JSON response only actually emits credential_id/name/sign_count (see
 * WebAuthnController.cc), but created_at/last_used_at are included here
 * since they are part of the query's selected columns and a future
 * caller may want to surface them.
 */
struct WebAuthnCredentialSummary
{
    std::string credentialId;
    std::string name;                   // Empty if NULL, mirroring the controller's isNull() check.
    int signCount = 0;
    int64_t createdAt = 0;              // Unix epoch seconds.
    std::optional<int64_t> lastUsedAt;   // nullopt if the credential has never been used.
};

/**
 * @brief Repository for WebAuthn/passkey credential state.
 */
class IWebAuthnRepository
{
  public:
    virtual ~IWebAuthnRepository() = default;

    using StoreCredentialCallback = std::function<void(StoreCredentialOutcome)>;
    using CredentialLookupCallback = std::function<void(std::optional<WebAuthnCredentialLookup>)>;
    using BoolCallback = std::function<void(bool)>;
    using ListCredentialsCallback = std::function<void(std::vector<WebAuthnCredentialSummary>)>;

    /// Store a newly registered credential for `userId` (registerFinish's
    /// INSERT). `signCount` starts at 0 and `created_at` is repository-
    /// owned (mirrors the controller's INSERT, which does not pass either
    /// column explicitly and relies on table defaults).
    virtual void storeCredential(
      int64_t userId,
      const std::string &credentialId,
      const std::string &publicKey,
      const std::string &name,
      StoreCredentialCallback &&cb
    ) = 0;

    /// Look up a credential by credential_id, joined with the owning
    /// user's public_sub (authenticateFinish's lookup query). nullopt if
    /// no such credential exists.
    virtual void findByCredentialId(const std::string &credentialId, CredentialLookupCallback &&cb) = 0;

    /// Update sign_count and last_used_at (= now) for a credential after
    /// a successful authentication (authenticateFinish's UPDATE).
    virtual void updateSignCount(
      const std::string &credentialId,
      int newSignCount,
      BoolCallback &&cb
    ) = 0;

    /// List all credentials belonging to `userId`, most recently created
    /// first (listCredentials' SELECT ... ORDER BY created_at DESC).
    virtual void listCredentials(int64_t userId, ListCredentialsCallback &&cb) = 0;
};

}  // namespace authforge::identity
