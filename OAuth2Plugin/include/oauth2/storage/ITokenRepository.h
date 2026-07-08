#pragma once

// M1 storage interface split (design.md §7). See IClientRepository.h header
// comment for the general rationale (additive, non-migrating, see
// REPOSITORY_MAPPING.md for the full mapping).
#include <oauth2/storage/IOAuth2Storage.h>

#include <functional>
#include <optional>
#include <string>

namespace oauth2
{

/**
 * @brief Repository for access tokens, refresh tokens, and their lifecycle
 * (introspection, revocation) -- the "token" aggregate.
 *
 * Carves out access/refresh token CRUD, the transactional saveTokenPair /
 * revokeTokenFamily contract, RFC 7662 introspection, and RFC 7009
 * revocation from the former god interface IOAuth2Storage. See
 * REPOSITORY_MAPPING.md for the full 30-method mapping.
 *
 * SDK consumers MUST implement this interface (design.md §7.1).
 *
 * Contract preserved from IOAuth2Storage (design.md §7.2):
 *  - saveTokenPair / revokeTokenFamily transactional atomicity is a
 *    per-implementation contract: the default implementation below runs
 *    sequentially (non-transactional), matching the existing Memory/Redis
 *    behavior; a Postgres override is expected to wrap both writes in a DB
 *    transaction (unchanged from current PostgresOAuth2Storage design).
 *  - supportsTransactions() / supportsCas() let contract tests (Task 12,
 *    design.md §7.3 "分档契约测试") select which implementations run the
 *    atomicity/CAS-tier tests. Every implementation MUST override these to
 *    truthfully declare its capability -- a false "true" is a contract
 *    violation the test suite is designed to catch ("能力谎报致 CI 失败").
 */
class ITokenRepository
{
  public:
    virtual ~ITokenRepository() = default;

    using AccessTokenCallback = std::function<void(std::optional<OAuth2AccessToken>)>;
    using RefreshTokenCallback = std::function<void(std::optional<OAuth2RefreshToken>)>;
    using VoidCallback = std::function<void()>;
    using TokenIntrospectionCallback = std::function<void(std::optional<TokenIntrospection>)>;

    // ========== Access Token Operations ==========

    /**
     * @brief Save a new access token.
     * Original: IOAuth2Storage::saveAccessToken
     */
    virtual void saveAccessToken(const OAuth2AccessToken &token, VoidCallback &&cb) = 0;

    /**
     * @brief Get access token by token value.
     * Original: IOAuth2Storage::getAccessToken
     */
    virtual void getAccessToken(const std::string &token, AccessTokenCallback &&cb) = 0;

    // ========== Token Pair Operations (Transactional) ==========

    /**
     * @brief Save access token + refresh token as an atomic pair.
     * Default implementation calls saveAccessToken then saveRefreshToken
     * sequentially (non-transactional), matching IOAuth2Storage's default.
     * A PostgreSQL-backed implementation should override this to use a
     * database transaction for real atomicity (supportsTransactions() ==
     * true in that case).
     *
     * Original: IOAuth2Storage::saveTokenPair (had a default body; that
     * default is reproduced verbatim here so no override behavior changes
     * for implementations that don't override it).
     */
    virtual void saveTokenPair(
      const OAuth2AccessToken &at,
      const OAuth2RefreshToken &rt,
      VoidCallback &&cb
    )
    {
        // Default: sequential (non-transactional) for Memory/Redis.
        saveAccessToken(at, [this, rt, cb = std::move(cb)]() mutable {
            saveRefreshToken(rt, std::move(cb));
        });
    }

    // ========== Refresh Token Operations ==========

    /**
     * @brief Save a new refresh token.
     * Original: IOAuth2Storage::saveRefreshToken
     */
    virtual void saveRefreshToken(const OAuth2RefreshToken &token, VoidCallback &&cb) = 0;

    /**
     * @brief Get refresh token by token value.
     * Original: IOAuth2Storage::getRefreshToken
     */
    virtual void getRefreshToken(const std::string &token, RefreshTokenCallback &&cb) = 0;

    /**
     * @brief Revoke a refresh token.
     * Original: IOAuth2Storage::revokeRefreshToken
     */
    virtual void revokeRefreshToken(const std::string &token, VoidCallback &&cb) = 0;

    /**
     * @brief Atomically revoke a refresh token (CAS operation).
     * Only revokes if the token is currently NOT revoked. Returns the token
     * data if successfully revoked, nullopt if already revoked. Used for
     * refresh token rotation to detect reuse.
     *
     * Original: IOAuth2Storage::atomicRevokeRefreshToken
     */
    virtual void atomicRevokeRefreshToken(const std::string &token, RefreshTokenCallback &&cb) = 0;

    /**
     * @brief Revoke all tokens in a refresh token family (cascade
     * revocation). Called when refresh token reuse is detected. Revokes all
     * refresh tokens AND their associated access tokens.
     *
     * Original: IOAuth2Storage::revokeTokenFamily
     */
    virtual void revokeTokenFamily(const std::string &familyId, VoidCallback &&cb) = 0;

    // ========== P1: Token Introspection (RFC 7662) ==========

    /**
     * @brief Introspect token metadata for RFC 7662 compliance.
     * Original: IOAuth2Storage::introspectToken
     */
    virtual void introspectToken(const std::string &token, TokenIntrospectionCallback &&cb) = 0;

    /**
     * @brief Increment introspection count for monitoring.
     * Original: IOAuth2Storage::incrementIntrospectCount
     */
    virtual void incrementIntrospectCount(const std::string &token, VoidCallback &&cb) = 0;

    // ========== P1: Token Revocation (RFC 7009) ==========

    /**
     * @brief Revoke an access token with audit trail.
     * Original: IOAuth2Storage::revokeAccessToken
     */
    virtual void revokeAccessToken(
      const std::string &token,
      const std::string &revokedBy,
      VoidCallback &&cb
    ) = 0;

    // ========== Cleanup ==========

    /**
     * @brief Purge expired access tokens and refresh tokens.
     *
     * Decision (see REPOSITORY_MAPPING.md): tokens have explicit
     * expiresAt fields and the original IOAuth2Storage::deleteExpiredData()
     * purged both access and refresh tokens in all three existing
     * implementations. Required pure-virtual method.
     *
     * Replaces the token portion of the former
     * IOAuth2Storage::deleteExpiredData(). Intended to be invoked by a
     * future product-level CleanupService (not implemented in Task 7).
     */
    virtual void purgeExpired() = 0;

    // ========== Capability flags (F5 contract tiering) ==========

    /**
     * @brief Whether this implementation provides true transactional
     * atomicity for saveTokenPair (and any other multi-write operation that
     * claims atomicity).
     *
     * Used by the tiered contract test suite (Task 12, design.md §7.3) to
     * decide whether to run the "atomicity/transaction" test tier against
     * this implementation. Implementations MUST answer truthfully:
     * declaring true without real transactional guarantees is a contract
     * violation the test suite is designed to catch.
     *
     * Expected answers per design.md: Postgres -> true; Memory/Redis ->
     * false (or a best-effort in-process-lock answer, per implementation;
     * see Task 10/12 for the actual decision on each backend).
     */
    virtual bool supportsTransactions() const = 0;

    /**
     * @brief Whether this implementation provides a true compare-and-swap
     * guarantee for atomicRevokeRefreshToken (and revokeTokenFamily's
     * cascade, where relevant).
     *
     * Used by the tiered contract test suite (Task 12, design.md §7.3) to
     * decide whether to run the CAS/concurrency test tier against this
     * implementation. Implementations MUST answer truthfully.
     */
    virtual bool supportsCas() const = 0;
};

}  // namespace oauth2
