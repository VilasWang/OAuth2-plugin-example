#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of RedisOAuth2Storage
// into per-aggregate implementation files, mirroring the Task 9 Postgres
// split. This one implements IGrantRepository (REPOSITORY_MAPPING.md #3-6,
// #22-25, plus the grant slice of #32 deleteExpiredData -> purgeExpired). It
// is ADDITIVE: RedisOAuth2Storage / IOAuth2Storage are untouched and remain
// the production path used by OAuth2Plugin.cc today.
#include <authforge/oauth2/repository/IGrantRepository.h>
#include <oauth2/storage/RedisRepositoryBase.h>

#include <memory>

namespace oauth2
{

// Task 27.5: now implements the NEW Domain-layer interface
// authforge::oauth2::repository::IGrantRepository (+ authforge::oauth2::model::* DTOs) instead of
// the legacy oauth2 one. Types are qualified below (not aliased into this namespace) because the
// legacy oauth2::* DTOs still coexist in IOAuth2Storage.h during this transition.
using IGrantRepositoryBase = ::authforge::oauth2::repository::IGrantRepository;

/**
 * @brief Redis implementation of IGrantRepository.
 *
 * Auth-code operations (saveAuthCode/getAuthCode/markAuthCodeUsed/
 * consumeAuthCode) are a faithful port of RedisOAuth2Storage's
 * implementation, including consumeAuthCode's Lua-script CAS ("GET, check
 * used, check redirect_uri, SET used=true") and its RFC 6749 §4.1.3
 * redirect_uri validation.
 *
 * Authorization-transaction operations
 * (saveAuthorizationTransaction/getAuthorizationTransaction/
 * deleteAuthorizationTransaction/markTransactionConsumed) are ALSO a
 * faithful port of the real (non-placeholder) Redis JSON-blob-per-key
 * implementation.
 *
 * purgeExpired(): Redis relies on key TTL (SETEX) for auth-code/transaction
 * expiry, exactly as RedisOAuth2Storage::deleteExpiredData() documented as a
 * no-op ("Redis deleteExpiredData called (No-op, relying on Redis TTL)").
 * This override preserves that no-op behavior verbatim -- it does NOT
 * implement real cleanup logic that the original never had.
 */
class RedisGrantRepository : public IGrantRepositoryBase,
                             public RedisRepositoryBase,
                             public std::enable_shared_from_this<RedisGrantRepository>
{
  public:
    explicit RedisGrantRepository(const std::string &redisClientName = "default")
        : RedisRepositoryBase(redisClientName)
    {
    }

    void saveAuthCode(
      const ::authforge::oauth2::model::OAuth2AuthCode &code,
      VoidCallback &&cb
    ) override;
    void getAuthCode(const std::string &code, AuthCodeCallback &&cb) override;
    void markAuthCodeUsed(const std::string &code, VoidCallback &&cb) override;
    void consumeAuthCode(
      const std::string &code,
      const std::string &redirectUri,
      AuthCodeCallback &&cb
    ) override;

    void saveAuthorizationTransaction(
      const ::authforge::oauth2::model::AuthorizationTransaction &transaction,
      BoolCallback &&cb
    ) override;
    void getAuthorizationTransaction(
      const std::string &transactionId,
      TransactionCallback &&cb
    ) override;
    void deleteAuthorizationTransaction(
      const std::string &transactionId,
      VoidCallback &&cb
    ) override;
    void markTransactionConsumed(const std::string &transactionId, BoolCallback &&cb) override;

    void purgeExpired() override;
};

}  // namespace oauth2
