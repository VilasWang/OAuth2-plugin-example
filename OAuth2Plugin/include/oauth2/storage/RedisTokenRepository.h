#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of RedisOAuth2Storage
// into per-aggregate implementation files, mirroring the Task 9 Postgres
// split. This one implements ITokenRepository (REPOSITORY_MAPPING.md #7-14,
// #29-31, plus the token slice of #32 deleteExpiredData -> purgeExpired,
// plus the supportsTransactions()/supportsCas() capability flags). It is
// ADDITIVE: RedisOAuth2Storage / IOAuth2Storage are untouched and remain the
// production path used by OAuth2Plugin.cc today.
#include <authforge/oauth2/repository/ITokenRepository.h>
#include <oauth2/storage/RedisRepositoryBase.h>

#include <memory>

namespace oauth2
{

// Task 27.5: now implements the NEW Domain-layer interface
// authforge::oauth2::repository::ITokenRepository (+ authforge::oauth2::model::* DTOs) instead of
// the legacy oauth2 one. Types are qualified below (not aliased into this namespace) because the
// legacy oauth2::* DTOs still coexist in IOAuth2Storage.h during this transition.
using ITokenRepositoryBase = ::authforge::oauth2::repository::ITokenRepository;

/**
 * @brief Redis implementation of ITokenRepository.
 *
 * Preserved-as-is current-state quirks (verbatim ports, NOT new decisions
 * made by this split -- see class-body comments on each method for the
 * "why" pointer back to the original RedisOAuth2Storage):
 *  - saveRefreshToken()/getRefreshToken() are no-ops (the original never
 *    actually stored/retrieved refresh tokens in Redis mode).
 *  - revokeTokenFamily() only logs a warning; it does not cascade-revoke
 *    (the original comment says "Redis: limited support").
 *  - purgeExpired() is a no-op relying on Redis key TTL.
 *
 * Capability flags -- these are Task 10's actual judgment calls, not carried
 * over from any prior declaration (ITokenRepository is new in Task 7; Redis
 * never declared these before):
 *
 *  - supportsTransactions() -> false. saveTokenPair() is NOT overridden here,
 *    so it falls through to ITokenRepository's default (sequential
 *    saveAccessToken() then saveRefreshToken()). And since
 *    saveRefreshToken() is itself a no-op (see above), "saveTokenPair" on
 *    Redis doesn't even persist the refresh half, let alone atomically --
 *    declaring true would be exactly the "能力谎报" (capability lie) the
 *    design doc's contract-test tiering (design.md §7.3) is built to catch.
 *
 *  - supportsCas() -> false, DESPITE atomicRevokeRefreshToken() implementing
 *    a real "get, check revoked, then set revoked" sequence. Two independent
 *    reasons keep this false rather than true:
 *      1. atomicRevokeRefreshToken() is layered on top of
 *         getRefreshToken()/revokeRefreshToken(), and getRefreshToken() is a
 *         no-op that always returns std::nullopt (see above) -- so the CAS
 *         path can never even observe a refresh token to revoke on this
 *         backend as currently wired. Declaring supportsCas() == true would
 *         promise a guarantee the method can't structurally deliver here.
 *      2. Even setting aside point 1, the underlying mechanism is a
 *         non-atomic "GET then SETEX" two-step (see the original
 *         RedisOAuth2Storage::atomicRevokeRefreshToken comment: "Redis
 *         doesn't have native CAS ... For simplicity, get then set") -- a
 *         second concurrent caller could interleave between the GET and the
 *         SET and also observe "not yet revoked", producing a double-revoke
 *         race that a real CAS (single atomic command / Lua script, as
 *         RedisGrantRepository's consumeAuthCode uses) would prevent. This
 *         is exactly the "尽力而为但不是真原子" situation the task
 *         description calls out; per design.md §7.3's "能力谎报致 CI 失败"
 *         rule, the conservative/truthful answer is false, not true.
 */
class RedisTokenRepository : public ITokenRepositoryBase,
                             public RedisRepositoryBase,
                             public std::enable_shared_from_this<RedisTokenRepository>
{
  public:
    explicit RedisTokenRepository(const std::string &redisClientName = "default")
        : RedisRepositoryBase(redisClientName)
    {
    }

    void saveAccessToken(
      const ::authforge::oauth2::model::OAuth2AccessToken &token,
      VoidCallback &&cb
    ) override;
    void getAccessToken(const std::string &token, AccessTokenCallback &&cb) override;

    // saveTokenPair() is intentionally NOT overridden: the ITokenRepository
    // default (sequential saveAccessToken then saveRefreshToken) matches
    // RedisOAuth2Storage's original behavior, which also never overrode
    // IOAuth2Storage::saveTokenPair.

    void saveRefreshToken(
      const ::authforge::oauth2::model::OAuth2RefreshToken &token,
      VoidCallback &&cb
    ) override;
    void getRefreshToken(const std::string &token, RefreshTokenCallback &&cb) override;
    void revokeRefreshToken(const std::string &token, VoidCallback &&cb) override;
    void atomicRevokeRefreshToken(const std::string &token, RefreshTokenCallback &&cb) override;
    void revokeTokenFamily(const std::string &familyId, VoidCallback &&cb) override;

    void introspectToken(const std::string &token, TokenIntrospectionCallback &&cb) override;
    void incrementIntrospectCount(const std::string &token, VoidCallback &&cb) override;
    void revokeAccessToken(
      const std::string &token,
      const std::string &revokedBy,
      VoidCallback &&cb
    ) override;

    void purgeExpired() override;

    bool supportsTransactions() const override
    {
        return false;
    }

    bool supportsCas() const override
    {
        return false;
    }
};

}  // namespace oauth2
