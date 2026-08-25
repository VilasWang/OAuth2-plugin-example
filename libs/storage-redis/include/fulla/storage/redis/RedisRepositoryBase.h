#pragma once

#include <drogon/nosql/RedisClient.h>
#include <string>

namespace fulla::storage::redis
{

/**
 * @brief Small shared mixin for the Redis repository split (Task 10,
 * design.md §7 / REPOSITORY_MAPPING.md), mirroring PostgresRepositoryBase's
 * rationale for the Postgres split (Task 9).
 *
 * The pre-split `RedisOAuth2Storage` fetched a single
 * `drogon::nosql::RedisClientPtr` in its constructor (by client name, default
 * "default"), set a 3-second timeout on it, and logged success/failure. All
 * seven repositories carved out of it (`RedisClientRepository`,
 * `RedisGrantRepository`, `RedisTokenRepository`, `RedisConsentRepository`,
 * `RedisUserRepository`, `RedisRoleRepository`,
 * `RedisSubjectMappingRepository`) need the exact same client member + the
 * exact same lookup/timeout/logging logic. Rather than copy/paste that seven
 * times (drift risk), this is a plain non-virtual mixin holding the one
 * `RedisClientPtr` member, initialized from a constructor parameter -- it
 * intentionally does NOT:
 *  - inherit `std::enable_shared_from_this<>` (each concrete repository
 *    inherits that itself, parameterized on its own concrete type, exactly
 *    as PostgresRepositoryBase's header comment explains for the Postgres
 *    split -- the safe `self = shared_from_this()` capture pattern is bound
 *    to the concrete class);
 *  - inherit any repository interface (it is a data/behavior mixin, not an
 *    `IXxxRepository` implementation);
 *  - take a `Json::Value config` like `PostgresRepositoryBase::initFromConfig`
 *    does. Redis's original constructor took a plain `redisClientName`
 *    string directly (`RedisOAuth2Storage(const std::string
 *    &redisClientName = "default")`), not a config block -- this mixin
 *    preserves that exact constructor-parameter shape instead of inventing a
 *    config-based init method that the original class never had.
 */
class RedisRepositoryBase
{
  public:
    /**
     * @brief Construct and fetch the named Redis client, mirroring
     * RedisOAuth2Storage's constructor body verbatim (getRedisClient +
     * setTimeout(3.0) + success/failure logging).
     */
    explicit RedisRepositoryBase(const std::string &redisClientName = "default");

    virtual ~RedisRepositoryBase() = default;

  protected:
    ::drogon::nosql::RedisClientPtr redisClient_;
};

}  // namespace fulla::storage::redis
