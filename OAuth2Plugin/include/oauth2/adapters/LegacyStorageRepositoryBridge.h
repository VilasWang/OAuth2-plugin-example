#pragma once

// M3 Task 24 slice 1 (authforge-sdk-refactor, PROGRESS.md "Task 24 切分
// 方案"): bridge adapters that let the NEW Domain-layer services
// (authforge::oauth2::protocol::TokenService/ClientService/
// AuthorizationService, Task 17) run against the EXISTING production
// storage implementations (MemoryOAuth2Storage/PostgresOAuth2Storage/
// RedisOAuth2Storage/CachedOAuth2Storage), none of which have been
// migrated to implement the new split repository interfaces
// (authforge::oauth2::repository::{IClientRepository,IGrantRepository,
// ITokenRepository,IConsentRepository}, Task 17 slice 3) -- they still
// implement the old god interface oauth2::IOAuth2Storage.
//
// Each bridge below is a thin, purely mechanical forwarding adapter: it
// holds a std::shared_ptr<oauth2::IOAuth2Storage> and translates each new
// interface method into the corresponding old interface method, doing a
// field-by-field DTO conversion between authforge::oauth2::model::* and
// the old (nested-in-IOAuth2Storage.h) oauth2::* structs. There is no
// business logic here -- this exists purely to unblock wiring the new
// TokenService/ClientService into production (Task 24) BEFORE the larger,
// separate effort of actually migrating the three concrete storage
// classes to implement the new interfaces directly (PROGRESS.md's "路径
// 2", deliberately deferred: 3 storage backends x 4 interfaces, plus
// re-architecting CachedOAuth2Storage per design.md §7.4, is a
// substantially larger task than Task 24 requires).
//
// Placement: OAuth2Plugin/include/oauth2/adapters/, alongside the other
// Adapter-layer port implementations (OpenSslCryptoProvider/DrogonLogger/
// StorageRoleProvider) -- these bridges depend on the OAuth2Plugin-side
// oauth2::IOAuth2Storage type, so they cannot live in libs/oauth2 (Domain
// layer must not depend on OAuth2Plugin, design.md §4.1 rule 1) or
// libs/drogon (these classes have zero Drogon dependency themselves, no
// reason to pull in Drogon just to reach them).
//
// Capability flags (supportsTransactions/supportsCas on
// ITokenRepositoryBridge): IOAuth2Storage has no such flags, but the
// underlying concrete class's transactional behavior is already known by
// OAuth2Plugin (it picks the concrete class based on config
// storage_type). The bridge constructor therefore takes the same
// storageType string OAuth2Plugin already tracks
// (OAuth2Plugin::storageType_) and derives the flags from it: "postgres"
// overrides saveTokenPair with a real DB transaction
// (PostgresOAuth2Storage::saveTokenPair) and atomicRevokeRefreshToken is a
// genuine compare-and-swap (row-level UPDATE ... WHERE revoked = false) in
// all three backends -- so supportsCas() is true unconditionally and
// supportsTransactions() is true only for "postgres".

#include <authforge/common/model/Subject.h>
#include <authforge/common/ports/IRoleProvider.h>
#include <authforge/common/ports/ISubjectResolver.h>
#include <authforge/oauth2/model/Dto.h>
#include <authforge/oauth2/model/UserRef.h>
#include <authforge/oauth2/repository/IClientRepository.h>
#include <authforge/oauth2/repository/IConsentRepository.h>
#include <authforge/oauth2/repository/IGrantRepository.h>
#include <authforge/oauth2/repository/ITokenRepository.h>
#include <oauth2/storage/IOAuth2Storage.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace oauth2::adapters
{

// ---------------------------------------------------------------------
// DTO conversion helpers (old oauth2::* <-> new authforge::oauth2::model::*)
// ---------------------------------------------------------------------
// Field-for-field, mechanical. Both sides are plain structs with
// identical field sets (Task 17 slice 2 ported these DTOs verbatim from
// IOAuth2Storage.h, see model/Dto.h's own header comment) -- the only
// real conversion is ClientType, which is a distinct enum type on each
// side with the same two enumerators.

inline authforge::oauth2::model::ClientType toNewClientType(::oauth2::ClientType t)
{
    return t == ::oauth2::ClientType::PUBLIC ? authforge::oauth2::model::ClientType::PUBLIC
                                              : authforge::oauth2::model::ClientType::CONFIDENTIAL;
}

inline ::oauth2::ClientType toOldClientType(authforge::oauth2::model::ClientType t)
{
    return t == authforge::oauth2::model::ClientType::PUBLIC ? ::oauth2::ClientType::PUBLIC
                                                              : ::oauth2::ClientType::CONFIDENTIAL;
}

inline authforge::oauth2::model::OAuth2Client toNewClient(const ::oauth2::OAuth2Client &c)
{
    authforge::oauth2::model::OAuth2Client out;
    out.clientId = c.clientId;
    out.clientType = toNewClientType(c.clientType);
    out.clientSecretHash = c.clientSecretHash;
    out.salt = c.salt;
    out.redirectUris = c.redirectUris;
    out.allowedScopes = c.allowedScopes;
    return out;
}

inline authforge::oauth2::model::OAuth2AuthCode toNewAuthCode(const ::oauth2::OAuth2AuthCode &c)
{
    authforge::oauth2::model::OAuth2AuthCode out;
    out.code = c.code;
    out.clientId = c.clientId;
    out.userId = c.userId;
    out.scope = c.scope;
    out.redirectUri = c.redirectUri;
    out.codeChallenge = c.codeChallenge;
    out.codeChallengeMethod = c.codeChallengeMethod;
    out.nonce = c.nonce;
    out.expiresAt = c.expiresAt;
    out.used = c.used;
    return out;
}

inline ::oauth2::OAuth2AuthCode toOldAuthCode(const authforge::oauth2::model::OAuth2AuthCode &c)
{
    ::oauth2::OAuth2AuthCode out;
    out.code = c.code;
    out.clientId = c.clientId;
    out.userId = c.userId;
    out.scope = c.scope;
    out.redirectUri = c.redirectUri;
    out.codeChallenge = c.codeChallenge;
    out.codeChallengeMethod = c.codeChallengeMethod;
    out.nonce = c.nonce;
    out.expiresAt = c.expiresAt;
    out.used = c.used;
    return out;
}

inline authforge::oauth2::model::OAuth2AccessToken toNewAccessToken(
  const ::oauth2::OAuth2AccessToken &t
)
{
    authforge::oauth2::model::OAuth2AccessToken out;
    out.token = t.token;
    out.clientId = t.clientId;
    out.userId = t.userId;
    out.scope = t.scope;
    out.expiresAt = t.expiresAt;
    out.revoked = t.revoked;
    out.issuedAt = t.issuedAt;
    out.issuer = t.issuer;
    out.audience = t.audience;
    out.notBefore = t.notBefore;
    out.introspectCount = t.introspectCount;
    out.revokedAt = t.revokedAt;
    out.revokedBy = t.revokedBy;
    return out;
}

inline ::oauth2::OAuth2AccessToken toOldAccessToken(
  const authforge::oauth2::model::OAuth2AccessToken &t
)
{
    ::oauth2::OAuth2AccessToken out;
    out.token = t.token;
    out.clientId = t.clientId;
    out.userId = t.userId;
    out.scope = t.scope;
    out.expiresAt = t.expiresAt;
    out.revoked = t.revoked;
    out.issuedAt = t.issuedAt;
    out.issuer = t.issuer;
    out.audience = t.audience;
    out.notBefore = t.notBefore;
    out.introspectCount = t.introspectCount;
    out.revokedAt = t.revokedAt;
    out.revokedBy = t.revokedBy;
    return out;
}

inline authforge::oauth2::model::OAuth2RefreshToken toNewRefreshToken(
  const ::oauth2::OAuth2RefreshToken &t
)
{
    authforge::oauth2::model::OAuth2RefreshToken out;
    out.token = t.token;
    out.accessToken = t.accessToken;
    out.clientId = t.clientId;
    out.userId = t.userId;
    out.scope = t.scope;
    out.expiresAt = t.expiresAt;
    out.revoked = t.revoked;
    out.familyId = t.familyId;
    out.revokedAt = t.revokedAt;
    out.revokedBy = t.revokedBy;
    return out;
}

inline ::oauth2::OAuth2RefreshToken toOldRefreshToken(
  const authforge::oauth2::model::OAuth2RefreshToken &t
)
{
    ::oauth2::OAuth2RefreshToken out;
    out.token = t.token;
    out.accessToken = t.accessToken;
    out.clientId = t.clientId;
    out.userId = t.userId;
    out.scope = t.scope;
    out.expiresAt = t.expiresAt;
    out.revoked = t.revoked;
    out.familyId = t.familyId;
    out.revokedAt = t.revokedAt;
    out.revokedBy = t.revokedBy;
    return out;
}

inline authforge::oauth2::model::TokenIntrospection toNewIntrospection(
  const ::oauth2::TokenIntrospection &t
)
{
    authforge::oauth2::model::TokenIntrospection out;
    out.active = t.active;
    out.clientId = t.clientId;
    out.tokenType = t.tokenType;
    out.exp = t.exp;
    out.iat = t.iat;
    out.nbf = t.nbf;
    out.sub = t.sub;
    out.aud = t.aud;
    out.iss = t.iss;
    out.scope = t.scope;
    return out;
}

inline authforge::oauth2::model::AuthorizationTransaction toNewTransaction(
  const ::oauth2::IOAuth2Storage::AuthorizationTransaction &t
)
{
    authforge::oauth2::model::AuthorizationTransaction out;
    out.transactionId = t.transactionId;
    out.clientId = t.clientId;
    out.subject = t.subject;
    out.redirectUri = t.redirectUri;
    out.state = t.state;
    out.codeChallenge = t.codeChallenge;
    out.codeChallengeMethod = t.codeChallengeMethod;
    out.requestedScopes = t.requestedScopes;
    out.validScopes = t.validScopes;
    out.consentRequiredScopes = t.consentRequiredScopes;
    out.consumed = t.consumed;
    out.expiresAt = t.expiresAt;
    return out;
}

inline ::oauth2::IOAuth2Storage::AuthorizationTransaction toOldTransaction(
  const authforge::oauth2::model::AuthorizationTransaction &t
)
{
    ::oauth2::IOAuth2Storage::AuthorizationTransaction out;
    out.transactionId = t.transactionId;
    out.clientId = t.clientId;
    out.subject = t.subject;
    out.redirectUri = t.redirectUri;
    out.state = t.state;
    out.codeChallenge = t.codeChallenge;
    out.codeChallengeMethod = t.codeChallengeMethod;
    out.requestedScopes = t.requestedScopes;
    out.validScopes = t.validScopes;
    out.consentRequiredScopes = t.consentRequiredScopes;
    out.consumed = t.consumed;
    out.expiresAt = t.expiresAt;
    return out;
}

// ---------------------------------------------------------------------
// IClientRepository bridge
// ---------------------------------------------------------------------

class ClientRepositoryBridge : public authforge::oauth2::repository::IClientRepository
{
  public:
    explicit ClientRepositoryBridge(std::shared_ptr<::oauth2::IOAuth2Storage> storage) :
      storage_(std::move(storage))
    {
    }

    void getClient(const std::string &clientId, ClientCallback &&cb) override
    {
        storage_->getClient(clientId, [cb = std::move(cb)](std::optional<::oauth2::OAuth2Client> c) {
            cb(c ? std::make_optional(toNewClient(*c)) : std::nullopt);
        });
    }

    void validateClient(
      const std::string &clientId,
      const std::string &clientSecret,
      BoolCallback &&cb
    ) override
    {
        storage_->validateClient(clientId, clientSecret, std::move(cb));
    }

  private:
    std::shared_ptr<::oauth2::IOAuth2Storage> storage_;
};

// ---------------------------------------------------------------------
// IGrantRepository bridge
// ---------------------------------------------------------------------

class GrantRepositoryBridge : public authforge::oauth2::repository::IGrantRepository
{
  public:
    explicit GrantRepositoryBridge(std::shared_ptr<::oauth2::IOAuth2Storage> storage) :
      storage_(std::move(storage))
    {
    }

    void saveAuthCode(
      const authforge::oauth2::model::OAuth2AuthCode &code,
      VoidCallback &&cb
    ) override
    {
        storage_->saveAuthCode(toOldAuthCode(code), std::move(cb));
    }

    void getAuthCode(const std::string &code, AuthCodeCallback &&cb) override
    {
        storage_->getAuthCode(code, [cb = std::move(cb)](std::optional<::oauth2::OAuth2AuthCode> c) {
            cb(c ? std::make_optional(toNewAuthCode(*c)) : std::nullopt);
        });
    }

    void markAuthCodeUsed(const std::string &code, VoidCallback &&cb) override
    {
        storage_->markAuthCodeUsed(code, std::move(cb));
    }

    void consumeAuthCode(
      const std::string &code,
      const std::string &redirectUri,
      AuthCodeCallback &&cb
    ) override
    {
        storage_->consumeAuthCode(
          code, redirectUri,
          [cb = std::move(cb)](std::optional<::oauth2::OAuth2AuthCode> c) {
              cb(c ? std::make_optional(toNewAuthCode(*c)) : std::nullopt);
          }
        );
    }

    void saveAuthorizationTransaction(
      const AuthorizationTransaction &transaction,
      BoolCallback &&cb
    ) override
    {
        storage_->saveAuthorizationTransaction(toOldTransaction(transaction), std::move(cb));
    }

    void getAuthorizationTransaction(
      const std::string &transactionId,
      TransactionCallback &&cb
    ) override
    {
        storage_->getAuthorizationTransaction(
          transactionId,
          [cb = std::move(cb)](std::optional<::oauth2::IOAuth2Storage::AuthorizationTransaction> t) {
              cb(t ? std::make_optional(toNewTransaction(*t)) : std::nullopt);
          }
        );
    }

    void deleteAuthorizationTransaction(const std::string &transactionId, VoidCallback &&cb)
      override
    {
        storage_->deleteAuthorizationTransaction(transactionId, std::move(cb));
    }

    void markTransactionConsumed(const std::string &transactionId, BoolCallback &&cb) override
    {
        storage_->markTransactionConsumed(transactionId, std::move(cb));
    }

    void purgeExpired() override
    {
        // IOAuth2Storage has no per-aggregate purge -- deleteExpiredData()
        // covers auth codes/transactions AND tokens together. Calling it
        // here would double-purge once TokenRepositoryBridge also calls
        // it. Left as a no-op: the existing OAuth2CleanupService already
        // calls storage_->deleteExpiredData() directly (see
        // OAuth2Plugin.cc), so this bridge does not need to duplicate
        // that wiring.
    }

  private:
    std::shared_ptr<::oauth2::IOAuth2Storage> storage_;
};

// ---------------------------------------------------------------------
// ITokenRepository bridge
// ---------------------------------------------------------------------

class TokenRepositoryBridge : public authforge::oauth2::repository::ITokenRepository
{
  public:
    TokenRepositoryBridge(std::shared_ptr<::oauth2::IOAuth2Storage> storage, std::string storageType) :
      storage_(std::move(storage)), storageType_(std::move(storageType))
    {
    }

    void saveAccessToken(
      const authforge::oauth2::model::OAuth2AccessToken &token,
      VoidCallback &&cb
    ) override
    {
        storage_->saveAccessToken(toOldAccessToken(token), std::move(cb));
    }

    void getAccessToken(const std::string &token, AccessTokenCallback &&cb) override
    {
        storage_->getAccessToken(
          token,
          [cb = std::move(cb)](std::optional<::oauth2::OAuth2AccessToken> t) {
              cb(t ? std::make_optional(toNewAccessToken(*t)) : std::nullopt);
          }
        );
    }

    void saveTokenPair(
      const authforge::oauth2::model::OAuth2AccessToken &at,
      const authforge::oauth2::model::OAuth2RefreshToken &rt,
      VoidCallback &&cb
    ) override
    {
        // Forward to the old interface's saveTokenPair (not the
        // ITokenRepository default sequential body) so the real
        // PostgresOAuth2Storage transactional override is preserved
        // verbatim, instead of losing atomicity by going through this
        // bridge's own default.
        storage_->saveTokenPair(toOldAccessToken(at), toOldRefreshToken(rt), std::move(cb));
    }

    void saveRefreshToken(
      const authforge::oauth2::model::OAuth2RefreshToken &token,
      VoidCallback &&cb
    ) override
    {
        storage_->saveRefreshToken(toOldRefreshToken(token), std::move(cb));
    }

    void getRefreshToken(const std::string &token, RefreshTokenCallback &&cb) override
    {
        storage_->getRefreshToken(
          token,
          [cb = std::move(cb)](std::optional<::oauth2::OAuth2RefreshToken> t) {
              cb(t ? std::make_optional(toNewRefreshToken(*t)) : std::nullopt);
          }
        );
    }

    void revokeRefreshToken(const std::string &token, VoidCallback &&cb) override
    {
        storage_->revokeRefreshToken(token, std::move(cb));
    }

    void atomicRevokeRefreshToken(const std::string &token, RefreshTokenCallback &&cb) override
    {
        storage_->atomicRevokeRefreshToken(
          token,
          [cb = std::move(cb)](std::optional<::oauth2::OAuth2RefreshToken> t) {
              cb(t ? std::make_optional(toNewRefreshToken(*t)) : std::nullopt);
          }
        );
    }

    void revokeTokenFamily(const std::string &familyId, VoidCallback &&cb) override
    {
        storage_->revokeTokenFamily(familyId, std::move(cb));
    }

    void introspectToken(const std::string &token, TokenIntrospectionCallback &&cb) override
    {
        storage_->introspectToken(
          token,
          [cb = std::move(cb)](std::optional<::oauth2::TokenIntrospection> t) {
              cb(t ? std::make_optional(toNewIntrospection(*t)) : std::nullopt);
          }
        );
    }

    void incrementIntrospectCount(const std::string &token, VoidCallback &&cb) override
    {
        storage_->incrementIntrospectCount(token, std::move(cb));
    }

    void revokeAccessToken(
      const std::string &token,
      const std::string &revokedBy,
      VoidCallback &&cb
    ) override
    {
        storage_->revokeAccessToken(token, revokedBy, std::move(cb));
    }

    void purgeExpired() override
    {
        // See GrantRepositoryBridge::purgeExpired()'s comment -- the
        // existing OAuth2CleanupService already drives
        // storage_->deleteExpiredData() directly; left as a no-op here to
        // avoid double-purging.
    }

    bool supportsTransactions() const override
    {
        // Only PostgresOAuth2Storage overrides saveTokenPair with a real
        // DB transaction (see PostgresOAuth2Storage.cc); Memory/Redis use
        // IOAuth2Storage's default sequential body.
        return storageType_ == "postgres";
    }

    bool supportsCas() const override
    {
        // atomicRevokeRefreshToken is implemented as a genuine
        // compare-and-swap (row-level conditional UPDATE) in all three
        // backends (Memory/Redis/Postgres) -- see each *OAuth2Storage.cc's
        // own atomicRevokeRefreshToken().
        return true;
    }

  private:
    std::shared_ptr<::oauth2::IOAuth2Storage> storage_;
    std::string storageType_;
};

// ---------------------------------------------------------------------
// IConsentRepository bridge
// ---------------------------------------------------------------------

class ConsentRepositoryBridge : public authforge::oauth2::repository::IConsentRepository
{
  public:
    explicit ConsentRepositoryBridge(std::shared_ptr<::oauth2::IOAuth2Storage> storage) :
      storage_(std::move(storage))
    {
    }

    void hasUserConsent(
      const authforge::oauth2::model::UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) override
    {
        storage_->hasUserConsent(user.internalUserId, clientId, scope, std::move(cb));
    }

    void saveUserConsent(
      const authforge::oauth2::model::UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      BoolCallback &&cb
    ) override
    {
        storage_->saveUserConsent(user.internalUserId, clientId, scope, std::move(cb));
    }

    void revokeUserConsent(
      const authforge::oauth2::model::UserRef &user,
      const std::string &clientId,
      const std::string &scope,
      VoidCallback &&cb
    ) override
    {
        storage_->revokeUserConsent(user.internalUserId, clientId, scope, std::move(cb));
    }

  private:
    std::shared_ptr<::oauth2::IOAuth2Storage> storage_;
};

// ---------------------------------------------------------------------
// ISubjectResolver / IRoleProvider bridge (legacy string-keyed roles)
// ---------------------------------------------------------------------
// M3 Task 24 slice 2 fix: the new TokenService resolves the "roles" field
// in its token-exchange response via the port chain
// ISubjectResolver::resolve(subject) -> IRoleProvider::getRoles(userId)
// (design.md §5.2). The OLD TokenService instead did a SINGLE hop:
// storage_->getUserRoles(subjectString, ...) -- a direct lookup keyed by
// the raw OAuth2 subject string, with NO subject-mapping-table
// involvement (see MemoryOAuth2Storage::getUserRoles(const string&),
// which looks up userRoles_ by the subject string verbatim, defaulting
// to {"user"} if absent -- entirely independent of
// getInternalUserId/subjectMappings_). Routing this through
// getInternalUserId() (subject-mapping lookup) then getUserRoles(int32_t)
// would silently return EMPTY roles for any subject that was never
// explicitly linked via createSubjectMapping -- a real behavior
// regression from the old single-hop call (caught by
// PluginTest.cc's "Verify Admin Roles" case during this slice's own
// verification).
//
// LegacyRoleResolutionBridge preserves the OLD single-hop semantics
// exactly while still satisfying the new two-port shape: it implements
// BOTH ports on one object. resolve() performs the OLD direct
// storage_->getUserRoles(subject.value(), ...) call immediately, stashes
// the resulting role list in a small mutex-guarded map keyed by a
// monotonically increasing synthetic id (never reused, so concurrent
// calls cannot collide), and hands that id back through the
// ISubjectResolver contract as if it were a resolved internal user id.
// getRoles() then simply looks up (and erases) that id's stashed roles.
// TokenService's own resolveRoles() always calls resolve() then, if a
// non-nullopt id came back, getRoles() with that SAME id, synchronously
// within the same async continuation chain -- so the stash is read
// exactly once, immediately after being written, then cleaned up.
//
// This is a deliberately narrow compatibility shim for this slice only:
// a real ISubjectResolver/IRoleProvider wiring (subject-mapping-table
// backed, shared with AuthorizationService) is future work once the
// broader identity/oauth2 assembly (Task 24's later slices) is in place.

class LegacyRoleResolutionBridge : public authforge::common::ports::ISubjectResolver,
                                    public authforge::common::ports::IRoleProvider
{
  public:
    explicit LegacyRoleResolutionBridge(std::shared_ptr<::oauth2::IOAuth2Storage> storage) :
      storage_(std::move(storage))
    {
    }

    void resolve(const authforge::common::model::Subject &subject, ResolveCallback &&cb) override
    {
        storage_->getUserRoles(
          subject.value(),
          [this, cb = std::move(cb)](std::vector<std::string> roles) {
              int32_t id = nextId_.fetch_add(1);
              {
                  std::lock_guard<std::mutex> lock(mutex_);
                  pendingRoles_[id] = std::move(roles);
              }
              cb(id);
          }
        );
    }

    void getRoles(int32_t internalUserId, RolesCallback &&cb) override
    {
        std::vector<std::string> roles;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = pendingRoles_.find(internalUserId);
            if (it != pendingRoles_.end())
            {
                roles = std::move(it->second);
                pendingRoles_.erase(it);
            }
        }
        cb(std::move(roles));
    }

  private:
    std::shared_ptr<::oauth2::IOAuth2Storage> storage_;
    std::mutex mutex_;
    std::unordered_map<int32_t, std::vector<std::string>> pendingRoles_;
    std::atomic<int32_t> nextId_{1};
};

}  // namespace oauth2::adapters
